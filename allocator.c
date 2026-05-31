/**
 * allocator_zhang.c - 张仕达负责版本
 * 
 * 在袁东霖版本基础上，添加：
 * 1. coalesce() 空闲块合并（4种情况）
 * 2. find_fit() 首次适配搜索
 * 3. place() 块分割
 * 4. my_malloc() 内存分配
 * 
 * Copyright (c) 2015 MIT License by 6.172 Staff
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

// All blocks must have a specified minimum alignment.
// The alignment requirement (from config.h) is >= 8 bytes.
#ifndef ALIGNMENT
  #define ALIGNMENT 8
#endif

// Rounds up to the nearest multiple of ALIGNMENT.
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

// The smallest aligned size that will hold a size_t value.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

//===================================================================================
// 袁东霖负责：宏定义
//===================================================================================

// 基本大小定义
#define WSIZE sizeof(size_t)  // 字大小（64位系统为8字节）
#define DSIZE (2*WSIZE)       // 双字大小（16字节）
#define CHUNKSIZE (1<<12)     // 扩展堆的默认大小（4096字节）

// 最大值
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// 打包和解包宏
#define PACK(size, alloc) ((size) | (alloc))  // 将大小和分配位打包成一个字

// 读写地址 p 处的字
#define GET(p) (*(size_t*)(p))                 // 读取指针p处的值
#define PUT(p, val) (*(size_t*)(p) = (val))    // 写入值到指针p处

// 从头部读取大小和分配位
#define GET_SIZE(p) (GET(p) & ~0x7)           // 获取块大小（清除低3位）
#define GET_ALLOC(p) (GET(p) & 0x1)           // 获取分配位（最低位）

// 给定块指针 bp，计算头部和脚部的地址
#define HDRP(bp) ((char*)(bp) - WSIZE)                              // 头部地址
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)        // 尾部地址

// 给定块指针 bp，计算下一个和上一个块的地址
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))           // 下一个块
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE)) // 上一个块

//===================================================================================
// 袁东霖负责：静态全局变量和函数声明
//===================================================================================

// 静态全局变量
static char* heap_listp = NULL;

// 函数声明
static void* extend_heap(size_t words);

//===================================================================================
// 袁东霖负责：my_check() - 堆检查器
//===================================================================================

/**
 * my_check - 检查堆的完整性
 * 返回：0表示成功，-1表示失败
 */
int my_check() {
  if (heap_listp == NULL) {
    printf("Heap not initialized\n");
    return -1;
  }
  
  void* bp = heap_listp;
  
  // 检查序言块
  if (GET_SIZE(HDRP(bp)) != DSIZE || !GET_ALLOC(HDRP(bp))) {
    printf("Bad prologue header\n");
    return -1;
  }
  
  // 遍历所有块
  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (GET_ALLOC(HDRP(bp))) {
      if (!GET_ALLOC(FTRP(bp))) {
        printf("Allocated block without footer\n");
        return -1;
      }
    } else {
      if (GET_ALLOC(FTRP(bp))) {
        printf("Free block with allocated footer\n");
        return -1;
      }
    }
  }
  
  // 检查结尾块
  if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
    printf("Bad epilogue header\n");
    return -1;
  }
  
  return 0;
}

//===================================================================================
// 袁东霖负责：my_init() - 堆初始化
//===================================================================================

/**
 * my_init - 初始化内存分配器
 * 返回：0表示成功，-1表示失败
 */
int my_init() {
  // 创建初始空堆：对齐填充 + 序言头部 + 序言脚部 + 结尾头部
  if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)
    return -1;
  
  PUT(heap_listp, 0);                            // 对齐填充（8字节）
  PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));   // 序言头部（16字节，已分配）
  PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));   // 序言脚部（16字节，已分配）
  PUT(heap_listp + (3*WSIZE), PACK(0, 1));       // 结尾头部（0字节，已分配）
  heap_listp += (2*WSIZE);  // 指向序言脚部
  
  // 扩展空堆（4096字节）
  if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
    return -1;
  
  return 0;
}

//===================================================================================
// 袁东霖负责：extend_heap() - 堆扩展
//===================================================================================

/**
 * extend_heap - 扩展堆
 * @words: 要扩展的字数
 * 返回：新空闲块的bp，失败返回NULL
 */
static void* extend_heap(size_t words) {
  char* bp;
  size_t size;
  
  // 分配偶数个字以保持8字节对齐
  size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
  char* old_brk = mem_sbrk(size);
  if (old_brk == (void*)-1) {
    return NULL;
  }
  
  // 关键：bp = old_brk，这样HDRP(bp)正好覆盖旧的结尾头部
  bp = old_brk;
  
  // 初始化空闲块头部/脚部和新的结尾头部
  PUT(HDRP(bp), PACK(size, 0));         // 空闲块头部
  PUT(FTRP(bp), PACK(size, 0));         // 空闲块脚部
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 新的结尾头部
  
  // 如果前一个块是空闲的，合并它们
  return coalesce(bp);
}

//===================================================================================
// 张仕达负责：coalesce() - 空闲块合并
//===================================================================================

/**
 * coalesce - 合并相邻空闲块
 * 
 * @bp: 当前空闲块的指针
 * 
 * 四种合并情况：
 * +---+---+---+---+
 * | 1 | 2 | 3 | 4 |
 * +---+---+---+---+
 * |前分|前分|前空|前空|
 * |后分|后空|后分|后空|
 * +---+---+---+---+
 * 
 * 返回：合并后的块指针
 */
static void* coalesce(void* bp) {
  size_t prev_alloc;
  size_t next_alloc;
  size_t size = GET_SIZE(HDRP(bp));
  
  // 检查前一个块的状态
  void* prev_bp = PREV_BLKP(bp);
  size_t prev_size = GET_SIZE(HDRP(prev_bp));
  if (prev_size == 0) {
    // 前一个是填充块（padding），视为已分配
    prev_alloc = 1;
  } else {
    prev_alloc = GET_ALLOC(FTRP(prev_bp));
  }
  
  // 检查后一个块的状态
  next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  
  // 情况1：前后都已分配，不合并
  if (prev_alloc && next_alloc) {
    return bp;
  }
  // 情况2：前已分配，后空闲，与后块合并
  else if (prev_alloc && !next_alloc) {
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  // 加上后块大小
    PUT(HDRP(bp), PACK(size, 0));           // 更新头部
    PUT(FTRP(bp), PACK(size, 0));           // 更新脚部（自动指向正确位置）
  }
  // 情况3：前空闲，后已分配，与前块合并
  else if (!prev_alloc && next_alloc) {
    size += GET_SIZE(HDRP(prev_bp));        // 加上前块大小
    PUT(FTRP(bp), PACK(size, 0));           // 更新脚部
    PUT(HDRP(prev_bp), PACK(size, 0));      // 更新前块头部
    bp = prev_bp;                           // bp指向前块
  }
  // 情况4：前后都空闲，与前后都合并
  else {
    size += GET_SIZE(HDRP(prev_bp)) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));  // 加上前块和后块大小
    PUT(HDRP(prev_bp), PACK(size, 0));      // 更新前块头部
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 更新后块脚部
    bp = prev_bp;                           // bp指向前块
  }
  
  return bp;
}

//===================================================================================
// 张仕达负责：find_fit() - 首次适配搜索
//===================================================================================

/**
 * find_fit - 首次适配搜索
 * 
 * @asize: 需要的块大小（包含头部和脚部）
 * 
 * 遍历堆中的所有块，找到第一个满足大小要求的空闲块
 * 时间复杂度：O(n)
 * 
 * 返回：找到的空闲块指针，没找到返回NULL
 */
static void* find_fit(size_t asize) {
  void* bp;
  
  // 从堆头开始遍历所有块
  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    // 找到第一个满足大小要求的空闲块
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      return bp;
    }
  }
  
  return NULL;  // 没找到合适的空闲块
}

//===================================================================================
// 张仕达负责：place() - 块分割
//===================================================================================

/**
 * place - 在空闲块中放置分配块
 * 
 * @bp: 空闲块指针
 * @asize: 需要的块大小（包含头部和脚部）
 * 
 * 如果剩余空间 >= 2*DSIZE（32字节），则分割块：
 * +--------+--------+--------+--------+
 * | 已分配 | 已分配 | 空闲头 | 空闲   |
 * | 头部   | 脚部   |        | 脚部   |
 * +--------+--------+--------+--------+
 * 
 * 否则，整个块分配给用户
 */
static void place(void* bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  
  // 如果剩余空间足够大（>=32字节），分割块
  if ((csize - asize) >= (2*DSIZE)) {
    // 分配前半部分
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    
    // 创建后半部分空闲块
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize-asize, 0));
    PUT(FTRP(bp), PACK(csize-asize, 0));
  }
  else {
    // 剩余空间不足，整个块分配
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

//===================================================================================
// 张仕达负责：my_malloc() - 内存分配
//===================================================================================

/**
 * my_malloc - 分配内存
 * 
 * @size: 请求的字节数
 * 
 * 工作流程：
 * 1. 计算实际需要的块大小：asize = MAX(2*DSIZE, ALIGN(size + DSIZE))
 *    - size + DSIZE：用户请求大小 + 头部和脚部空间
 *    - 2*DSIZE：最小块大小（32字节）
 * 2. 搜索空闲链表（find_fit）
 * 3. 如果找到，分割块（place）
 * 4. 如果没找到，扩展堆（extend_heap），然后place
 * 
 * 返回：分配的内存指针，失败返回NULL
 */
void* my_malloc(size_t size) {
  size_t asize;
  size_t extendsize;
  char* bp;
  
  if (size == 0) return NULL;
  
  // 调整块大小以包含头部和脚部，并满足对齐要求
  // 关键公式：asize = MAX(最小块大小, ALIGN(size + 头部 + 脚部))
  asize = MAX(2*DSIZE, ALIGN(size + DSIZE));
  //                     ^^^^^^^^^^^^^^^^
  //                     size + 16字节（头部8 + 脚本8）
  
  // 搜索空闲链表
  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }
  
  // 没找到合适的空闲块，扩展堆
  extendsize = MAX(asize, CHUNKSIZE);
  if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
    return NULL;
  
  place(bp, asize);
  return bp;
}

//===================================================================================
// 郭晓伟负责：my_free, my_realloc（在allocator_guo.c中实现）
//===================================================================================

void my_free(void* ptr) {
  // TODO: 郭晓伟实现
}

void* my_realloc(void* ptr, size_t size) {
  // TODO: 郭晓伟实现
  return NULL;
}
