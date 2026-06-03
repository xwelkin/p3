# PESS-writeup

## 一、个人分工

在本次Project 3内存分配器实验中，我主要负责**allocator.c释放重分配部分**的实现。具体包括：

1. **内存释放**：实现my_free()函数，清除分配位并调用coalesce合并相邻空闲块
2. **内存重分配**：实现my_realloc()函数，分配新块、复制旧数据、释放旧块
3. **Bug修复**：修复asize计算错误、coalesce边界访问错误、extend_heap指针错误
4. **测试验证**：运行测试用例，验证分配器正确性

## 二、实验方案

### 2.1 my_free 设计思路

`my_free` 函数负责释放已分配的内存块，主要步骤：
1. **清除分配位**：将头部和脚部的分配位设为0，标记为空闲
2. **合并相邻空闲块**：调用 `coalesce()` 函数，尝试与前后空闲块合并

### 2.2 my_realloc 设计思路

`my_realloc` 函数负责重新分配内存，主要步骤：
1. **特殊情况处理**：ptr为NULL时等同于malloc，size为0时等同于free
2. **分配新块**：调用 `my_malloc(size)` 分配新大小的内存
3. **复制旧数据**：使用 `memcpy` 复制旧数据到新块
4. **释放旧块**：调用 `my_free(ptr)` 释放旧内存

**公式**：

```
payload大小 = 块大小 - DSIZE（头部 + 脚部）
复制大小 = min(old_payload_size, new_size)
```

## 三、核心实现

### 3.1 my_free()实现

```c
void my_free(void* ptr) {
  if (ptr == NULL) return;
  
  size_t size = GET_SIZE(HDRP(ptr));
  
  // 清除分配位（标记为空闲）
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  
  // 尝试合并相邻空闲块
  coalesce(ptr);
}
```

**实现要点**：
- 首先检查ptr是否为NULL
- 从头部读取块大小
- 清除头部和脚部的分配位（设为0）
- 调用coalesce()合并相邻空闲块

### 3.2 my_realloc()实现

```c
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
```

**实现要点**：
- 处理特殊情况：ptr为NULL时等同于malloc，size为0时等同于free
- 先分配新块，再复制旧数据，最后释放旧块
- payload大小 = 块大小 - DSIZE（头部8字节 + 脚部8字节）
- 只复制min(old_payload_size, new_size)字节

## 四、测试结果

### 4.1 最终测试结果（20个trace）

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

**结论**：20个trace全部通过，`valid=yes`，总体平均利用率77.5%，综合评分18.62/100

## 五、耗时表

| 阶段 | 任务内容 | 耗时 |
|------|----------|------|
| my_free实现 | 实现内存释放函数 | 1h |
| my_realloc实现 | 实现内存重分配函数 | 1.5h |
| 测试验证 | 运行所有测试用例和结果分析 | 0.5h |
| **总计** | | **3h** |

---

**实验心得**：

调试是本次实验最耗时的部分，但也是收获最大的部分。通过调试，我深入理解了：

1. **内存布局的重要性**：指针计算必须精确，差一个字节就会导致严重错误
2. **边界条件的处理**：序言块、结尾块、填充区域等特殊块需要特殊处理
3. **数据完整性验证**：realloc不仅要复制数据，还要确保复制的正确性
