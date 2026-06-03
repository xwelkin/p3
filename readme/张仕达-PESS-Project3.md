# PESS-writeup

## 一、个人分工

在本次Project 3内存分配器实验中，我主要负责**allocator.c核心分配逻辑**的实现。具体包括：

1. **空闲块合并**：实现coalesce()函数，处理4种合并情况
2. **首次适配搜索**：实现find_fit()函数，线性扫描空闲链表
3. **块分割**：实现place()函数，将空闲块分割为已分配块和剩余空闲块
4. **内存分配**：实现my_malloc()函数，计算asize并调用find_fit和place

## 二、实验方案

### 2.1 隐式空闲链表原理

隐式空闲链表是一种简单的内存管理方式：
- 每个块包含**头部**（Header）和**尾部**（Footer），存储块大小和分配状态
- 通过**指针算术**遍历所有块，无需额外的链表指针
- 分配时搜索空闲块，释放时合并相邻空闲块

### 2.2 设计决策

1. **块大小对齐**：所有块大小必须是8字节的倍数
2. **最小块大小**：32字节（2*DSIZE），确保能存储头部和尾部
3. **搜索策略**：首次适配（First Fit），简单高效
4. **合并策略**：立即合并（Immediate Coalescing）

### 2.3 算法流程

**malloc流程**：
1. 调整请求大小为对齐后的块大小
2. 搜索空闲链表找到合适的块
3. 如果找到，分割块并返回
4. 如果未找到，扩展堆并重新搜索

**free流程**：
1. 标记块为空闲（清除分配位）
2. 检查相邻块是否空闲
3. 如果相邻块空闲，合并它们

## 三、核心实现

### 3.1 堆初始化 - my_init()

```c
int my_init() {
  // 创建初始空堆
  if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)
    return -1;
  PUT(heap_listp, 0);                            // 对齐填充
  PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));   // 序言头部
  PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));   // 序言脚部
  PUT(heap_listp + (3*WSIZE), PACK(0, 1));       // 结尾头部
  heap_listp += (2*WSIZE);
  
  // 扩展空堆
  if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
    return -1;
  return 0;
}
```

**设计要点**：
- 序言块（Prologue Block）：大小为DSIZE，始终标记为已分配
- 结尾块（Epilogue Header）：大小为0，标记为已分配，作为堆结束标志

### 3.2 堆扩展 - extend_heap()

```c
static void* extend_heap(size_t words) {
  char* bp;
  size_t size;
  
  // 分配偶数个字以保持对齐
  size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
  char* old_brk = mem_sbrk(size);
  if (old_brk == (void*)-1) {
    return NULL;
  }
  
  // old_brk 是当前 brk 指针位置
  // 结尾头部在 old_brk - WSIZE 位置
  // 新块的头部应该在 old_brk - WSIZE 位置（覆盖旧的结尾头部）
  // 新块的 payload 应该在 old_brk 位置
  bp = old_brk;
  
  // 初始化空闲块头部/脚部和结尾头部
  PUT(HDRP(bp), PACK(size, 0));         // 空闲块头部
  PUT(FTRP(bp), PACK(size, 0));         // 空闲块脚部
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 新的结尾头部
  
  // 如果前一个块是空闲的，合并
  return coalesce(bp);
}
```

**关键点**：
- `bp = old_brk`确保`HDRP(bp) = old_brk - WSIZE`覆盖旧的结尾头部
- 扩展后立即调用`coalesce()`合并相邻空闲块

### 3.3 空闲块合并 - coalesce()

```c
static void* coalesce(void* bp) {
  size_t prev_alloc;
  size_t next_alloc;
  size_t size = GET_SIZE(HDRP(bp));
  
  // 检查前一个块是否是序言块（通过检查前一个块头部的大小）
  void* prev_bp = PREV_BLKP(bp);
  size_t prev_size = GET_SIZE(HDRP(prev_bp));
  if (prev_size == 0) {
    // 前一个块是填充块（padding），视为已分配
    prev_alloc = 1;
  } else {
    prev_alloc = GET_ALLOC(FTRP(prev_bp));
  }
  
  next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  
  if (prev_alloc && next_alloc) {          // 情况1：前后都已分配
    return bp;
  }
  else if (prev_alloc && !next_alloc) {    // 情况2：前已分配，后空闲
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  }
  else if (!prev_alloc && next_alloc) {    // 情况3：前空闲，后已分配
    size += GET_SIZE(HDRP(prev_bp));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(prev_bp), PACK(size, 0));
    bp = prev_bp;
  }
  else {                                   // 情况4：前后都空闲
    size += GET_SIZE(HDRP(prev_bp)) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(prev_bp), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = prev_bp;
  }
  return bp;
}
```

**四种合并情况**：

| 情况 | 前块 | 后块 | 操作 |
|------|------|------|------|
| 1 | 已分配 | 已分配 | 不合并 |
| 2 | 已分配 | 空闲 | 与后块合并 |
| 3 | 空闲 | 已分配 | 与前块合并 |
| 4 | 空闲 | 空闲 | 与前后块都合并 |

### 3.4 首次适配搜索 - find_fit()

```c
static void* find_fit(size_t asize) {
  void* bp;
  
  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      return bp;
    }
  }
  return NULL;
}
```

**特点**：
- 线性扫描整个堆
- 返回第一个满足大小要求的空闲块
- 时间复杂度：O(n)，n为块的数量

### 3.5 块分割 - place()

```c
static void place(void* bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  
  if ((csize - asize) >= (2*DSIZE)) {  // 剩余空间足够分割
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize-asize, 0));
    PUT(FTRP(bp), PACK(csize-asize, 0));
  }
  else {  // 剩余空间不足，整个块分配
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}
```

**分割条件**：剩余空间必须>= 2*DSIZE（32字节），确保能形成一个完整的新空闲块。

### 3.6 内存分配 - my_malloc()

```c
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
```

**关键公式**：
- `asize = MAX(2*DSIZE, ALIGN(size + DSIZE))`
- `size + DSIZE`：用户请求大小 + 头部和尾部空间
- `2*DSIZE`：最小块大小（32字节）

### 3.7 内存释放 - my_free()

```c
void my_free(void* ptr) {
  if (ptr == NULL) return;
  
  size_t size = GET_SIZE(HDRP(ptr));
  
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  coalesce(ptr);
}
```

**释放过程**：
1. 清除头部和尾部的分配位
2. 调用`coalesce()`合并相邻空闲块

### 3.8 内存重分配 - my_realloc()

```c
void* my_realloc(void* ptr, size_t size) {
  if (ptr == NULL) return my_malloc(size);
  if (size == 0) { my_free(ptr); return NULL; }
  
  void* newptr = my_malloc(size);
  if (newptr == NULL) return NULL;
  
  // 复制旧数据 - payload 大小是块大小减去头部和脚部
  size_t oldsize = GET_SIZE(HDRP(ptr)) - DSIZE;
  if (size < oldsize) oldsize = size;
  memcpy(newptr, ptr, oldsize);
  
  my_free(ptr);
  return newptr;
}
```

**实现策略**：
- 分配新块
- 复制旧数据（取较小的大小）
- 释放旧块

## 四、正确性保障与性能验证

### 4.1 测试结果

使用`test_allocator`测试基本功能：

```
Step 1: mem_init
Step 2: my_init
init succeeded
Step 3: my_malloc(100)
malloc(100) returned 0000000000C2B060
Step 4: my_free(p1)
free succeeded
Step 5: my_malloc(50)
malloc(50) returned 0000000000C2B060
Step 6: my_realloc(p2, 200)
realloc(200) returned 0000000000C2B0A0
Step 7: my_free(p3)
free succeeded
Step 8: my_check
check succeeded
All tests passed!
```

### 4.2 mdriver测试结果（20个trace）

**traces目录（10个trace - v0版本）**：

```
Results for mm malloc:
trace                   filename     valid   checked  util     ops      secs Kops/sec
 0                   trace_c1_v0       yes       yes   93%   25958  0.014719     1764
 1                   trace_c5_v0       yes       yes   93%   30610  0.017834     1716
 2                   trace_c0_v0       yes       yes   99%    5694  0.003779     1507
 3                   trace_c8_v0       yes       yes   81%    8402  0.004794     1753
 4                   trace_c3_v0       yes       yes   59%    5768  0.010740      537
 5                   trace_c7_v0       yes       yes   99%    3840  0.000046    82759
 6                   trace_c4_v0       yes       yes   84%   18604  0.081325      229
 7                   trace_c9_v0       yes       yes   27%   14401  0.030964      465
 8                   trace_c2_v0       yes       yes   93%    4800  0.004066     1181
 9                   trace_c6_v0       yes       yes   80%    7996  0.002866     2789
Geometric Mean                                         76%  126073  0.171133     1620

# GeometricMean(76.317183 (util),  3.531972 (tput))  =  16.417983
correct:10
perfidx:16.417983
```

**additional_traces目录（10个trace - v1版本）**：

```
Results for mm malloc:
trace                   filename     valid   checked  util     ops      secs Kops/sec
 0                   trace_c7_v1       yes        no   98%    5358  0.000063    84645
 1                   trace_c0_v1       yes        no   99%    5848  0.003312     1766
 2                   trace_c3_v1       yes        no   62%    8290  0.019089      434
 3                   trace_c9_v1       yes        no   49%   14401  0.000538    26788
 4                   trace_c6_v1       yes        no   71%    7792  0.003258     2391
 5                   trace_c1_v1       yes        no   93%   27380  0.015055     1819
 6                   trace_c8_v1       yes        no   62%    6548  0.002206     2969
 7                   trace_c4_v1       yes        no   87%   18072  0.068675      263
 8                   trace_c5_v1       yes        no   83%   30610  0.016761     1826
 9                   trace_c2_v1       yes        no   91%    4800  0.003918     1225
Geometric Mean                                         79%  129099  0.132876     2583

# GeometricMean(78.633853 (util),  5.509152 (tput))  =  20.813598
correct:10
perfidx:20.813598
```

**平均利用率**：77.5%（20个trace平均）
**综合评分**：18.62/100（20个trace平均）

## 五、耗时表

| 阶段 | 任务内容 | 耗时 |
|------|----------|------|
| coalesce实现 | 实现4种合并情况 | 2h |
| find_fit实现 | 实现首次适配搜索 | 0.5h |
| place实现 | 实现块分割 | 1h |
| my_malloc实现 | 实现内存分配（asize计算+调用find_fit/place） | 1.5h |
| 测试验证 | 运行测试用例，验证正确性 | 0.5h |
| **总计** | | **5.5h** |

---

**实验心得**：

隐式空闲链表的实现让我深入理解了内存分配器的核心原理。最关键的难点是：
1. **coalesce合并**：4种情况的处理需要仔细分析，特别是前块为空闲时需要更新前块的头部
2. **find_fit搜索**：首次适配虽然简单，但O(n)的时间复杂度是性能瓶颈
3. **place分割**：分割条件需要确保剩余空间足够大（>=2*DSIZE）才能形成新的空闲块
