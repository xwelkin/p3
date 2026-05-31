/**
 * allocator_guo.c - 郭晓伟负责版本（完整版）
 * 
 * 在张仕达版本基础上，添加：
 * 1. my_free() 内存释放
 * 2. my_realloc() 内存重分配
 * 
 * 这是最终完整版本，包含所有功能
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
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);

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
 * @bp: 当前空闲块的指针
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
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }
  // 情况3：前空闲，后已分配，与前块合并
  else if (!prev_alloc && next_alloc) {
    size += GET_SIZE(HDRP(prev_bp));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(prev_bp), PACK(size, 0));
    bp = prev_bp;
  }
  // 情况4：前后都空闲，与前后都合并
  else {
    size += GET_SIZE(HDRP(prev_bp)) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(prev_bp), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = prev_bp;
  }
  
  return bp;
}

//===================================================================================
// 张仕达负责：find_fit() - 首次适配搜索
//===================================================================================

/**
 * find_fit - 首次适配搜索
 * @asize: 需要的块大小
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
 * @bp: 空闲块指针
 * @asize: 需要的块大小
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
 * @size: 请求的字节数
 * 返回：分配的内存指针，失败返回NULL
 */
void* my_malloc(size_t size) {
  size_t asize;
  size_t extendsize;
  char* bp;
  
  if (size == 0) return NULL;
  
  // 调整块大小以包含头部和脚部，并满足对齐要求
  asize = MAX(2*DSIZE, ALIGN(size + DSIZE));
  
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
// 郭晓伟负责：my_free() - 内存释放
//===================================================================================

/**
 * my_free - 释放内存
 * 
 * @ptr: 要释放的内存指针
 * 
 * 工作流程：
 * 1. 获取块大小
 * 2. 清除头部和脚部的分配位（标记为空闲）
 * 3. 调用coalesce合并相邻空闲块
 * 
 * 为什么释放后要合并？
 * - 减少外部碎片
 * - 提高后续分配的成功率
 */
void my_free(void* ptr) {
  if (ptr == NULL) return;
  
  size_t size = GET_SIZE(HDRP(ptr));
  
  // 清除分配位（标记为空闲）
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  
  // 尝试合并相邻空闲块
  coalesce(ptr);
}

//===================================================================================
// 郭晓伟负责：my_realloc() - 内存重分配
//===================================================================================

/**
 * my_realloc - 重新分配内存
 * 
 * @ptr: 旧内存指针
 * @size: 新的字节数
 * 
 * 工作流程：
 * 1. 如果ptr为NULL，等同于malloc(size)
 * 2. 如果size为0，等同于free(ptr)
 * 3. 分配新大小的内存
 * 4. 复制旧数据到新内存
 * 5. 释放旧内存
 * 
 * 关键点：
 * - payload大小 = 块大小 - DSIZE（头部+脚部）
 * - 只复制min(old_payload_size, new_size)字节
 * 
 * 返回：新内存指针，失败返回NULL
 */
void* my_realloc(void* ptr, size_t size) {
  // 特殊情况处理
  if (ptr == NULL) return my_malloc(size);
  if (size == 0) { my_free(ptr); return NULL; }
  
  // 分配新块
  void* newptr = my_malloc(size);
  if (newptr == NULL) return NULL;
  
  // 复制旧数据
  // 关键：payload大小 = 块大小 - 头部 - 脚部 = 块大小 - DSIZE
  size_t oldsize = GET_SIZE(HDRP(ptr)) - DSIZE;
  if (size < oldsize) oldsize = size;
  memcpy(newptr, ptr, oldsize);
  
  // 释放旧块
  my_free(ptr);
  
  return newptr;
}
