/*
 * validator_cheng.h - 程传哲负责版本
 * 
 * 负责内容：
 * 1. add_range() - 添加分配范围（对齐检查+边界检查+重叠检查+记录范围）
 * 2. remove_range() - 移除分配范围（双指针链表删除）
 * 3. eval_mm_valid() - 验证分配器正确性（数据完整性验证）
 * 
 * Copyright (c) 2010, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#ifndef MM_VALIDATOR_H
#define MM_VALIDATOR_H

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./config.h"
#include "./mdriver.h"
#include "./memlib.h"

// Returns true if p is R_ALIGNMENT-byte aligned
#if (__WORDSIZE == 64 )
  #define IS_ALIGNED(p)  ((((uint64_t)(p)) % R_ALIGNMENT) == 0)
#else
  #define IS_ALIGNED(p)  ((((uint32_t)(p)) % R_ALIGNMENT) == 0)
#endif

//===================================================================================
// 程传哲负责：数据结构定义
//===================================================================================

// Range list data structure
// Records the extent of each block's payload
typedef struct range_t {
  char* lo;              // low payload address
  char* hi;              // high payload address
  struct range_t* next;  // next list element
} range_t;

//===================================================================================
// 程传哲负责：add_range() - 添加分配范围
//===================================================================================

/**
 * add_range - 添加分配范围到链表
 * 
 * @impl: 分配器实现（用于获取堆边界）
 * @ranges: range链表的头指针的指针
 * @lo: 新分配块的payload起始地址
 * @size: payload大小
 * @tracenum: trace编号（用于错误报告）
 * @opnum: 操作编号（用于错误报告）
 * 
 * 验证内容：
 * 1. 对齐检查：地址必须8字节对齐
 * 2. 边界检查：必须在堆范围内
 * 3. 重叠检查：不能与已分配块重叠
 * 
 * 返回：1表示成功，0表示失败
 */
static int add_range(const malloc_impl_t* impl, range_t** ranges, char* lo,
                     int size, int tracenum, int opnum) {
  //  char *hi = lo + size - 1;
  //  range_t *p = NULL;

  // You can use this as a buffer for writing messages with sprintf.
  // char msg[MAXLINE];

  assert(size > 0);

  //===================================================================================
  // 检查1：对齐检查（8字节对齐）
  //===================================================================================
  // Payload addresses must be R_ALIGNMENT-byte aligned
  if (!IS_ALIGNED(lo)) {
    malloc_error(tracenum, opnum, "Payload is not aligned.");
    return 0;
  }

  //===================================================================================
  // 检查2：边界检查（在堆范围内）
  //===================================================================================
  // The payload must lie within the extent of the heap
  char* heap_lo = (char*)impl->heap_lo();
  char* heap_hi = (char*)impl->heap_hi() + 1;
  if (lo < heap_lo || (lo + size) > heap_hi) {
    malloc_error(tracenum, opnum, "Payload is out of heap bounds.");
    return 0;
  }

  //===================================================================================
  // 检查3：重叠检查（不与已分配块重叠）
  //===================================================================================
  // The payload must not overlap any other payloads
  range_t* p;
  for (p = *ranges; p != NULL; p = p->next) {
    // 重叠条件：lo < p->hi && (lo + size) > p->lo
    if (lo < p->hi && (lo + size) > p->lo) {
      malloc_error(tracenum, opnum, "Payload overlaps with another block.");
      return 0;
    }
  }

  //===================================================================================
  // 记录新范围：插入链表头部
  //===================================================================================
  // Everything looks OK, so remember the extent of this block by creating a
  // range struct and adding it the range list.
  range_t* new_range = (range_t*)malloc(sizeof(range_t));
  new_range->lo = lo;
  new_range->hi = lo + size - 1;
  new_range->next = *ranges;
  *ranges = new_range;

  return 1;
}

//===================================================================================
// 程传哲负责：remove_range() - 移除分配范围
//===================================================================================

/**
 * remove_range - 从链表中移除分配范围
 * 
 * @ranges: range链表的头指针的指针
 * @lo: 要移除的块的payload起始地址
 * 
 * 使用双指针技巧实现链表删除：
 * - prevpp：指向"指向当前节点的指针"
 * - 找到匹配节点后，*prevpp = p->next
 */
static void remove_range(range_t** ranges, char* lo) {
  range_t* p = *ranges;
  range_t** prevpp = ranges;
  
  while (p != NULL) {
    if (p->lo == lo) {
      // 找到匹配节点，从链表中移除
      *prevpp = p->next;
      free(p);
      return;
    }
    // 移动到下一个节点
    prevpp = &(p->next);
    p = p->next;
  }
}

//===================================================================================
// 程传哲负责：clear_ranges() - 清空所有范围
//===================================================================================

/**
 * clear_ranges - 清空所有范围记录
 * 
 * @ranges: range链表的头指针的指针
 * 
 * 遍历链表，释放每个节点，最后将头指针设为NULL
 */
static void clear_ranges(range_t** ranges) {
  range_t* p;
  range_t* pnext;

  for (p = *ranges; p != NULL; p = pnext) {
    pnext = p->next;  // 先保存下一个节点
    free(p);          // 释放当前节点
  }
  *ranges = NULL;
}

//===================================================================================
// 程传哲负责：eval_mm_valid() - 验证分配器正确性
//===================================================================================

/**
 * eval_mm_valid - 验证分配器的正确性
 * 
 * @impl: 分配器实现
 * @trace: trace数据
 * @tracenum: trace编号
 * 
 * 验证流程：
 * 1. 重置堆，调用init
 * 2. 遍历所有操作：
 *    - ALLOC：调用malloc，验证返回值，记录范围，填充测试数据0xAB
 *    - REALLOC：调用realloc，移除旧范围，添加新范围，验证数据完整性
 *    - FREE：移除范围，调用free
 *    - WRITE：忽略（只用于速度测试）
 * 3. 清理并返回
 * 
 * 返回：1表示成功，0表示失败
 */
int eval_mm_valid(const malloc_impl_t* impl, trace_t* trace, int tracenum) {
  int i = 0;
  int index = 0;
  int size = 0;
  int oldsize = 0;
  char* newp = NULL;
  char* oldp = NULL;
  char* p = NULL;
  range_t* ranges = NULL;

  // Reset the heap.
  impl->reset_brk();

  // Call the mm package's init function
  if (impl->init() < 0) {
    malloc_error(tracenum, 0, "impl init failed.");
    return 0;
  }

  // Interpret each operation in the trace in order
  for (i = 0; i < trace->num_ops; i++) {
    index = trace->ops[i].index;
    size = trace->ops[i].size;

    switch (trace->ops[i].type) {
    //===================================================================================
    // ALLOC：分配内存
    //===================================================================================
    case ALLOC:  // malloc

      // Call the student's malloc
      if ((p = (char*) impl->malloc(size)) == NULL) {
        malloc_error(tracenum, i, "impl malloc failed.");
        return 0;
      }

      // Test the range of the new block for correctness and add it
      // to the range list if OK. The block must be  be aligned properly,
      // and must not overlap any currently allocated block.
      if (add_range(impl, &ranges, p, size, tracenum, i) == 0) {
        return 0;
      }

      // Fill the allocated region with some unique data that you can check
      // for if the region is copied via realloc.
      memset(p, 0xAB, size);  // 填充测试数据

      // Remember region
      trace->blocks[index] = p;
      trace->block_sizes[index] = size;
      break;

    //===================================================================================
    // REALLOC：重新分配内存
    //===================================================================================
    case REALLOC:  // realloc

      // Call the student's realloc
      oldp = trace->blocks[index];
      if ((newp = (char*) impl->realloc(oldp, size)) == NULL) {
        malloc_error(tracenum, i, "impl realloc failed.");
        return 0;
      }

      // Remove the old region from the range list
      remove_range(&ranges, oldp);

      // Check new block for correctness and add it to range list
      if (add_range(impl, &ranges, newp, size, tracenum, i) == 0) {
        return 0;
      }

      // Make sure that the new block contains the data from the old block,
      // and then fill in the new block with new data that you can use to
      // verify the block was copied if it is resized again.
      oldsize = trace->block_sizes[index];
      if (size < oldsize) {
        oldsize = size;
      }
      
      //===================================================================================
      // 验证数据完整性：新块必须包含旧数据
      //===================================================================================
      // 直接比较新旧数据，而不是检查固定值
      for (int j = 0; j < oldsize; j++) {
        if (newp[j] != oldp[j]) {
          malloc_error(tracenum, i, "Realloc did not preserve data.");
          return 0;
        }
      }
      
      // 填充新数据
      memset(newp, 0xAB, size);

      // Remember region
      trace->blocks[index] = newp;
      trace->block_sizes[index] = size;
      break;

    //===================================================================================
    // FREE：释放内存
    //===================================================================================
    case FREE:  // free

      // Remove region from list and call student's free function
      p = trace->blocks[index];
      remove_range(&ranges, p);  // 从范围链表中移除
      impl->free(p);             // 调用分配器的free函数
      break;

    //===================================================================================
    // WRITE：写入操作（只用于速度测试，不验证）
    //===================================================================================
    case WRITE:  // write

      break;

    default:
      app_error("Nonexistent request type in eval_mm_valid");
    }
  }

  // Free ranges allocated and reset the heap.
  impl->reset_brk();
  clear_ranges(&ranges);

  // As far as we know, this is a valid malloc package
  return 1;
}
#endif  // MM_VALIDATOR_H
