/**
 * allocator.c - 最终进阶版 (四人协同优化)
 *
 * 核心优化特性：
 * 1. 分离空闲链表 (Segregated Free Lists) - 张仕达
 * 2. Footer 消除 (Footer Elimination) - 袁东霖
 * 3. 原地重分配 (In-place Realloc) - 郭晓伟
 * 4. 自动化参数调优钩子 (Autotuning) - 程传哲
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"

#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

#ifndef ALIGNMENT
  #define ALIGNMENT 8
#endif

#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

// =========================================================================
// 袁东霖 / 程传哲 负责：宏定义重构与自动调优参数
// =========================================================================

#define WSIZE sizeof(size_t)
#define DSIZE (2*WSIZE)

// 开放给 OpenTuner 调优的参数
#ifndef CHUNKSIZE
  #define CHUNKSIZE (1<<12)  // 默认每次扩展 4096 字节
#endif

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Footer 消除标志位
#define ALLOC_BIT 0x1
#define PREV_ALLOC_BIT 0x2   // 倒数第二位，标记前一个块是否已分配

// 打包大小、前一块状态和当前状态
#define PACK(size, prev_alloc, alloc) ((size) | (prev_alloc) | (alloc))

// 指针操作宏
#define GET(p) (*(size_t*)(p))
#define PUT(p, val) (*(size_t*)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & ALLOC_BIT)
#define GET_PREV_ALLOC(p) (GET(p) & PREV_ALLOC_BIT)

// 修改下一个块的状态宏
#define SET_PREV_ALLOC(p)   (GET(p) |= PREV_ALLOC_BIT)
#define CLEAR_PREV_ALLOC(p) (GET(p) &= ~PREV_ALLOC_BIT)

// 计算头部和脚部
#define HDRP(bp) ((char*)(bp) - WSIZE)
// 注意：只有空闲块才有 Footer！
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 计算前后块指针
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))
// 注意：只有前一个块是空闲块时，才能使用 PREV_BLKP！
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

// =========================================================================
// 张仕达 负责：分离链表数据结构 (Segregated Lists)
// =========================================================================

#define LIST_MAX 15

// 将空闲块的 Payload 强转为前后继指针
#define PRED_PTR(bp) ((char*)(bp))
#define SUCC_PTR(bp) ((char*)(bp) + WSIZE)

#define PRED(bp) (*(char**)(bp))
#define SUCC(bp) (*(char**)(SUCC_PTR(bp)))

static void *seg_lists[LIST_MAX];
static char *heap_listp = NULL;

// 辅助函数声明
static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void place(void* bp, size_t asize);
static void insert_node(void *bp, size_t size);
static void delete_node(void *bp);

// 映射大小到链表索引
static int get_list_index(size_t size) {
    if (size <= 32) return 0;
    if (size <= 64) return 1;
    if (size <= 128) return 2;
    if (size <= 256) return 3;
    if (size <= 512) return 4;
    if (size <= 1024) return 5;
    if (size <= 2048) return 6;
    if (size <= 4096) return 7;
    if (size <= 8192) return 8;
    if (size <= 16384) return 9;
    if (size <= 32768) return 10;
    if (size <= 65536) return 11;
    if (size <= 131072) return 12;
    if (size <= 262144) return 13;
    return 14;
}

// 插入空闲块到分离链表 (LIFO 策略)
static void insert_node(void *bp, size_t size) {
    int list_index = get_list_index(size);
    void *current_head = seg_lists[list_index];

    if (current_head != NULL) {
        PRED(current_head) = bp;
    }
    SUCC(bp) = current_head;
    PRED(bp) = NULL;
    seg_lists[list_index] = bp;
}

// 从分离链表移除空闲块
static void delete_node(void *bp) {
    int list_index = get_list_index(GET_SIZE(HDRP(bp)));

    if (PRED(bp) != NULL) {
        SUCC(PRED(bp)) = SUCC(bp);
    } else {
        seg_lists[list_index] = SUCC(bp);
    }
    if (SUCC(bp) != NULL) {
        PRED(SUCC(bp)) = PRED(bp);
    }
}

// =========================================================================
// 袁东霖 / 张仕达 负责：堆初始化与边界消除合并
// =========================================================================

int my_init() {
    int i;
    for (i = 0; i < LIST_MAX; i++) {
        seg_lists[i] = NULL;
    }

    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)
        return -1;

    PUT(heap_listp, 0); // 对齐填充
    // 序言块：将其标记为已分配，且默认它的前序也是分配的
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, PREV_ALLOC_BIT, 1)); 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, PREV_ALLOC_BIT, 1)); 
    // 结尾块
    PUT(heap_listp + (3*WSIZE), PACK(0, PREV_ALLOC_BIT, 1)); 
    
    heap_listp += (2*WSIZE);

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void* extend_heap(size_t words) {
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    // 关键：保留旧 Epilogue 头部的 PREV_ALLOC 状态
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    PUT(HDRP(bp), PACK(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK(size, prev_alloc, 0));
    // 新的 Epilogue，前一个块（刚才分配的空闲块）是空闲的，所以 prev_alloc 为 0
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1)); 

    return coalesce(bp);
}

// 合并相邻的空闲块，严格控制 Footer 状态
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        // 情况 1：前后都已分配
        CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(bp))); // 告诉下一个块：我变空闲了
        insert_node(bp, size);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        // 情况 2：与后块合并
        delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, PREV_ALLOC_BIT, 0));
        PUT(FTRP(bp), PACK(size, PREV_ALLOC_BIT, 0));
        insert_node(bp, size);
        return bp;
    }
    else if (!prev_alloc && next_alloc) {
        // 情况 3：与前块合并
        void *prev_bp = PREV_BLKP(bp);
        delete_node(prev_bp);
        size += GET_SIZE(HDRP(prev_bp));
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp));
        
        PUT(HDRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        PUT(FTRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        CLEAR_PREV_ALLOC(HDRP(NEXT_BLKP(bp))); 
        insert_node(prev_bp, size);
        return prev_bp;
    }
    else {
        // 情况 4：前后块都合并
        void *prev_bp = PREV_BLKP(bp);
        delete_node(prev_bp);
        delete_node(NEXT_BLKP(bp));
        
        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp));
        
        PUT(HDRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        PUT(FTRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        insert_node(prev_bp, size);
        return prev_bp;
    }
}

// =========================================================================
// 张仕达 / 郭晓伟 负责：核心分配逻辑
// =========================================================================

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    delete_node(bp);

    if ((csize - asize) >= (2 * DSIZE)) {
        // 分割逻辑：当前块分配，没有 Footer
        PUT(HDRP(bp), PACK(asize, prev_alloc, 1));

        void *next_bp = NEXT_BLKP(bp);
        // 新生空闲块：前一块(bp)已分配，所以打上 PREV_ALLOC_BIT
        PUT(HDRP(next_bp), PACK(csize - asize, PREV_ALLOC_BIT, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, PREV_ALLOC_BIT, 0));
        insert_node(next_bp, csize - asize);
    } else {
        // 全部分配
        PUT(HDRP(bp), PACK(csize, prev_alloc, 1));
        // 将真正的下一个块标记为前块已分配
        SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));
        // 【关键修复】如果下一个块是空闲的，改变它的 Header 后必须同步改变它的 Footer
        if (!GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
            PUT(FTRP(NEXT_BLKP(bp)), PACK(next_size, PREV_ALLOC_BIT, 0));
        }
    }
}

void* my_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    void *bp = NULL;

    if (size == 0) return NULL;

    // Footer消除后，Payload只需附加8字节的Header，再进行对齐
    asize = MAX(2 * DSIZE, ALIGN(size + WSIZE));

    // 在分离链表中搜索 (First Fit within Segregated List)
    int list_index = get_list_index(asize);
    for (; list_index < LIST_MAX; list_index++) {
        bp = seg_lists[list_index];
        while (bp != NULL && asize > GET_SIZE(HDRP(bp))) {
            bp = SUCC(bp);
        }
        if (bp != NULL) break; // 找到了
    }

    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    // 没有合适块，扩展堆
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

void my_free(void* bp) {
    if (bp == NULL) return;
    
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    // 释放时必须补回 Footer 供合并使用
    PUT(HDRP(bp), PACK(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK(size, prev_alloc, 0));

    coalesce(bp);
}

// 原地重分配逻辑
void* my_realloc(void* ptr, size_t size) {
    if (ptr == NULL) return my_malloc(size);
    if (size == 0) { my_free(ptr); return NULL; }

    size_t asize = MAX(2 * DSIZE, ALIGN(size + WSIZE));
    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));

    // 情况 1: 缩小内存 (原地切分)
    if (asize <= old_size) {
        if (old_size - asize >= (2 * DSIZE)) {
            PUT(HDRP(ptr), PACK(asize, prev_alloc, 1));
            
            void *next_bp = NEXT_BLKP(ptr);
            // 剩下的切分为空闲块并释放它
            PUT(HDRP(next_bp), PACK(old_size - asize, PREV_ALLOC_BIT, 1)); // 伪装成已分配交由 free 处理
            my_free(next_bp); 
        }
        return ptr;
    }

    // 情况 2: 扩大内存，且下一个块正好是空闲的并足够大
    void *next_bp = NEXT_BLKP(ptr);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));

    if (!next_alloc && (old_size + next_size >= asize)) {
        delete_node(next_bp); // 将下一个块从空闲链表中抽离
        // 物理合并
        PUT(HDRP(ptr), PACK(old_size + next_size, prev_alloc, 1));
        
        // 更新新的下一个块的标志位
        SET_PREV_ALLOC(HDRP(NEXT_BLKP(ptr)));
        if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
            size_t nn_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
            PUT(FTRP(NEXT_BLKP(ptr)), PACK(nn_size, PREV_ALLOC_BIT, 0));
        }
        return ptr;
    }

    // 情况 3: 只能兜底新开辟空间 (Fallback)
    void* newptr = my_malloc(size);
    if (newptr == NULL) return NULL;
    memcpy(newptr, ptr, old_size - WSIZE); // 只拷贝 payload
    my_free(ptr);
    return newptr;
}

// =========================================================================
// 程传哲 负责：极致严格的堆一致性检查器 (Heap Checker)
// =========================================================================

int my_check(void) {
    void *bp;
    int free_list_count = 0;
    int free_heap_count = 0;

    if (heap_listp == NULL) {
        printf("Error: Heap not initialized\n");
        return -1;
    }

    // 1. 检查序言块 (Prologue)
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
        printf("Error: Bad prologue header\n");
        return -1;
    }

    // 2. 遍历整个堆，检查基本不变量
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        // 检查 8 字节对齐
        if ((size_t)bp % 8 != 0) {
            printf("Error: Block at %p is not 8-byte aligned\n", bp);
            return -1;
        }

        size_t size = GET_SIZE(HDRP(bp));
        size_t alloc = GET_ALLOC(HDRP(bp));
        size_t next_prev_alloc = GET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));

        // 【核心校验 1】：检查 PREV_ALLOC 状态与前一个块的实际分配状态是否一致
        if (alloc && !next_prev_alloc) {
            printf("Error: Block %p is allocated, but next block's PREV_ALLOC is 0\n", bp);
            return -1;
        }
        if (!alloc && next_prev_alloc) {
            printf("Error: Block %p is free, but next block's PREV_ALLOC is 1\n", bp);
            return -1;
        }

        // 针对空闲块的特殊检查
        if (!alloc) {
            free_heap_count++;
            
            // 【核心校验 2】：空闲块必须有 Footer，且必须和 Header 完全一致
            if (GET_SIZE(FTRP(bp)) != size || GET_ALLOC(FTRP(bp)) != 0 || GET_PREV_ALLOC(FTRP(bp)) != GET_PREV_ALLOC(HDRP(bp))) {
                printf("Error: Free block %p header and footer mismatch\n", bp);
                return -1;
            }
            
            // 【核心校验 3】：不能有两个连续的空闲块（说明 coalesce 合并失效了）
            if (!GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
                printf("Error: Two consecutive free blocks detected at %p\n", bp);
                return -1;
            }
        }
    }

    // 3. 检查分离链表 (Segregated Lists) 内部的一致性
    for (int i = 0; i < LIST_MAX; i++) {
        void *node = seg_lists[i];
        while (node != NULL) {
            free_list_count++;
            
            // 检查指针是否越界（必须在堆范围内）
            if (node < mem_heap_lo() || node > (void*)((char*)mem_heap_hi() + 1)) {
                printf("Error: Free list node %p out of heap bounds in list %d\n", node, i);
                return -1;
            }
            
            // 检查挂在链表里的块是否真的被标记为了 free
            if (GET_ALLOC(HDRP(node))) {
                printf("Error: Allocated block %p found in free list %d\n", node, i);
                return -1;
            }
            
            node = SUCC(node);
        }
    }

    // 4. 交叉验证：链表中的节点总数，必须等于堆中遍历出的空闲块总数
    if (free_list_count != free_heap_count) {
        printf("Error: Free list count (%d) != Heap free block count (%d)\n", free_list_count, free_heap_count);
        return -1;
    }

    return 0; // 堆结构完美，没有任何损坏
}