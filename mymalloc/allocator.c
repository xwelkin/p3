/**
 * allocator_yuan.c - 袁东霖负责版本
 * 
 * 负责内容：
 * 1. 所有宏定义（WSIZE/DSIZE/PACK/GET/PUT/HDRP/FTRP/NEXT_BLKP/PREV_BLKP）
 * 2. my_init() 堆初始化
 * 3. extend_heap() 堆扩展
 * 4. my_check() 堆检查器
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

// 函数声明（张仕达负责实现）
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);

// 函数声明（袁东霖负责实现）
static void* extend_heap(size_t words);

//===================================================================================
// 袁东霖负责：my_check() - 堆检查器
//===================================================================================

/**
 * my_check - 检查堆的完整性
 * 
 * 遍历堆中的所有块，验证：
 * 1. 序言块的头部正确（大小=DSIZE，已分配）
 * 2. 每个块的头部和脚部一致
 * 3. 结尾块的头部正确（大小=0，已分配）
 * 
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
    // 检查头部和脚部是否一致
    if (GET_ALLOC(HDRP(bp))) {
      // 已分配块：脚部的分配位应该也是1
      if (!GET_ALLOC(FTRP(bp))) {
        printf("Allocated block without footer\n");
        return -1;
      }
    } else {
      // 空闲块：脚本的分配位应该也是0
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
 * 
 * 创建初始堆布局：
 * +--------+--------+--------+--------+
 * | 填充(0)| 序言头 | 序言脚 | 结尾头 |
 * | 8字节  | 16|1   | 16|1   | 0|1    |
 * +--------+--------+--------+--------+
 *          ^        ^
 *          |        heap_listp
 *       序言头部
 * 
 * 然后调用extend_heap扩展堆，创建初始空闲块
 * 
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
  heap_listp += (2*WSIZE);  // 指向序言脚部（第一个可分配位置）
  
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
 * 
 * @words: 要扩展的字数（会被调整为偶数以保持对齐）
 * 
 * 工作流程：
 * 1. 调用mem_sbrk分配内存
 * 2. 设置新空闲块的头部和脚部
 * 3. 设置新的结尾头部
 * 4. 调用coalesce合并相邻空闲块（张仕达负责实现）
 * 
 * 关键点：bp = old_brk，这样HDRP(bp)正好覆盖旧的结尾头部
 * 
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
  
  // 关键：bp = old_brk
  // 这样 HDRP(bp) = old_brk - WSIZE 正好覆盖旧的结尾头部
  bp = old_brk;
  
  // 初始化空闲块头部/脚部和新的结尾头部
  PUT(HDRP(bp), PACK(size, 0));         // 空闲块头部
  PUT(FTRP(bp), PACK(size, 0));         // 空闲块脚部
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 新的结尾头部
  
  // 如果前一个块是空闲的，合并它们
  return coalesce(bp);
}

//===================================================================================
// 以下是张仕达和郭晓伟负责实现的函数
// 张仕达：coalesce, find_fit, place, my_malloc
// 郭晓伟：my_free, my_realloc
//===================================================================================

// 占位函数声明（实际实现在其他版本中）
static void* coalesce_placeholder(void* bp) { return bp; }
static void* find_fit_placeholder(size_t asize) { return NULL; }
static void place_placeholder(void* bp, size_t asize) {}

void* my_malloc(size_t size) {
  // TODO: 张仕达实现
  return NULL;
}

void my_free(void* ptr) {
  // TODO: 郭晓伟实现
}

void* my_realloc(void* ptr, size_t size) {
  // TODO: 郭晓伟实现
  return NULL;
}
