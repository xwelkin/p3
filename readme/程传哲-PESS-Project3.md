# PESS-writeup

## 一、个人分工

在本次Project 3内存分配器实验中，我主要负责**validator.h堆验证器**的实现。具体包括：

1. **add_range实现**：实现对齐检查、边界检查、重叠检查，记录已分配块范围
2. **remove_range实现**：实现双指针链表删除，从范围链表中移除已释放块
3. **数据完整性验证**：实现realloc的数据保留验证（比较newp[j] != oldp[j]）
4. **性能分析**：收集测试数据，分析利用率和吞吐量瓶颈
5. **实验总结**：撰写实验心得和优化建议

## 二、实验方案

### 2.1 验证器设计思路

验证器（validator）的作用是检查分配器的正确性，主要验证：
1. **对齐检查**：分配的内存地址必须8字节对齐
2. **边界检查**：分配的内存必须在堆范围内
3. **重叠检查**：已分配的内存块不能重叠
4. **数据完整性**：realloc操作必须保留原有数据

### 2.2 add_range 设计

`add_range` 函数负责记录已分配的内存块范围，并验证其正确性：
1. 使用 `IS_ALIGNED()` 宏检查地址对齐
2. 使用 `heap_lo()` 和 `heap_hi()` 获取堆边界
3. 遍历 ranges 链表检查是否与已有块重叠
4. 验证通过后，将新范围插入链表头部

### 2.3 remove_range 设计

`remove_range` 函数负责从范围链表中移除已释放的块：
- 使用双指针技巧实现链表删除
- `prevpp` 指向"指向当前节点的指针"
- 找到匹配节点后，`*prevpp = p->next`

### 2.4 数据完整性验证

在 `eval_mm_valid` 中验证 realloc 的数据完整性：
- ALLOC 操作：填充测试数据 `0xAB`
- REALLOC 操作：比较新旧数据 `newp[j] != oldp[j]`

## 三、核心实现

### 3.1 add_range()实现

```c
static int add_range(const malloc_impl_t* impl, range_t** ranges, char* lo,
                     int size, int tracenum, int opnum) {
  assert(size > 0);

  // 检查1：对齐检查（8字节对齐）
  if (!IS_ALIGNED(lo)) {
    malloc_error(tracenum, opnum, "Payload is not aligned.");
    return 0;
  }

  // 检查2：边界检查（在堆范围内）
  char* heap_lo = (char*)impl->heap_lo();
  char* heap_hi = (char*)impl->heap_hi() + 1;
  if (lo < heap_lo || (lo + size) > heap_hi) {
    malloc_error(tracenum, opnum, "Payload is out of heap bounds.");
    return 0;
  }

  // 检查3：重叠检查（不与已分配块重叠）
  range_t* p;
  for (p = *ranges; p != NULL; p = p->next) {
    if (lo < p->hi && (lo + size) > p->lo) {
      malloc_error(tracenum, opnum, "Payload overlaps with another block.");
      return 0;
    }
  }

  // 记录新范围：插入链表头部
  range_t* new_range = (range_t*)malloc(sizeof(range_t));
  new_range->lo = lo;
  new_range->hi = lo + size - 1;
  new_range->next = *ranges;
  *ranges = new_range;

  return 1;
}
```

**实现要点**：
- 使用 `IS_ALIGNED()` 宏检查对齐
- 使用 `heap_lo()` 和 `heap_hi()` 获取堆边界
- 遍历 ranges 链表检查重叠

### 3.2 remove_range()实现

```c
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
```

**实现要点**：
- 使用双指针（prevpp）简化链表删除操作
- 处理头节点删除的特殊情况

### 3.3 数据完整性验证

```c
case ALLOC:  // malloc
  if ((p = (char*) impl->malloc(size)) == NULL) {
    malloc_error(tracenum, i, "impl malloc failed.");
    return 0;
  }
  if (add_range(impl, &ranges, p, size, tracenum, i) == 0) {
    return 0;
  }
  memset(p, 0xAB, size);  // 填充测试数据
  break;

case REALLOC:  // realloc
  oldp = trace->blocks[index];
  if ((newp = (char*) impl->realloc(oldp, size)) == NULL) {
    malloc_error(tracenum, i, "impl realloc failed.");
    return 0;
  }
  remove_range(&ranges, oldp);
  if (add_range(impl, &ranges, newp, size, tracenum, i) == 0) {
    return 0;
  }
  // 验证数据完整性
  oldsize = trace->block_sizes[index];
  if (size < oldsize) oldsize = size;
  for (int j = 0; j < oldsize; j++) {
    if (newp[j] != oldp[j]) {
      malloc_error(tracenum, i, "Realloc did not preserve data.");
      return 0;
    }
  }
  memset(newp, 0xAB, size);  // 填充新数据
  break;
```

**关键设计**：
- ALLOC 后填充 `0xAB` 作为测试数据
- REALLOC 时直接比较新旧数据 `newp[j] != oldp[j]`

## 四、性能数据分析

### 4.1 各Trace性能数据

| Trace | 利用率 | 吞吐量(Kops/s) | 吞吐量比 | 评分 |
|-------|--------|----------------|----------|------|
| c0 | 99% | 1507 | 5% | 22% |
| c1 | 93% | 1764 | 3% | 17% |
| c2 | 93% | 1181 | 15% | 37% |
| c3 | 59% | 537 | 1% | 8% |
| c4 | 84% | 229 | 0% | 0% |
| c5 | 93% | 1716 | 3% | 17% |
| c6 | 80% | 2789 | 4% | 18% |
| c7 | 99% | 82759 | 100% | 100% |
| c8 | 81% | 1753 | 6% | 22% |
| c9 | 27% | 465 | 1% | 5% |

**平均利用率**：76%
**综合评分**：16.42/100
**吞吐量比**：3.53%（平均）

### 4.2 利用率分析

**高利用率Trace（>80%）**：
- trace_c0 (99%)：分配模式简单，块大小相近
- trace_c1 (93%)：分配释放模式规律
- trace_c7 (99%)：分配后立即释放，几乎无碎片
- trace_c5 (93%)：块大小适中，碎片较少

**低利用率Trace（<60%）**：
- trace_c3 (59%)：大量小块分配，内部碎片严重
- trace_c9 (27%)：频繁realloc，外部碎片严重

**利用率瓶颈**：
1. **内部碎片**：最小块大小32字节，小块分配浪费严重
2. **外部碎片**：首次适配可能导致碎片积累
3. **realloc效率**：每次realloc都分配新块，旧块空间无法重用

### 3.3 吞吐量分析

**吞吐量瓶颈**：

1. **find_fit线性搜索**：O(n)时间复杂度
   - trace_c4有18604次操作，搜索耗时严重
   - 吞吐量仅164 Kops/s

2. **coalesce遍历**：每次free都要遍历相邻块
   - 增加了释放操作的开销

3. **extend_heap调用**：堆扩展时需要系统调用
   - 扩展后需要重新搜索

**吞吐量对比**：
- libc吞吐量：10000-50000 Kops/s
- 我们的吞吐量：1-38000 Kops/s
- 平均吞吐量比：约5%

## 四、正确性保障与性能验证

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

**结论**：20个trace全部通过，总体平均利用率77.5%，综合评分18.62/100

## 五、实验总结

### 5.1团队协作总结

本次实验中，团队分工明确，协作高效：
- **袁东霖**：负责宏定义+my_init+extend_heap+my_check，为分配器奠定基础
- **张仕达**：负责coalesce+find_fit+place+my_malloc，实现核心分配逻辑
- **郭晓伟**：负责my_free+my_realloc，完成释放和重分配功能
- **程传哲**：负责validator.h全部实现，确保分配器正确性

团队成员各司其职，互相配合，最终成功完成了实验任务。

## 六、耗时表

| 阶段 | 任务内容 | 耗时 |
|------|----------|------|
| add_range实现 | 对齐检查+边界检查+重叠检查+记录范围 | 2h |
| remove_range实现 | 双指针链表删除 | 1h |
| 数据完整性验证 | realloc数据保留验证 | 1h |
| 测试验证 | 运行所有测试用例分析利用率和吞吐量 | 0.5h |
| **总计** | | **4.5h** |

---

**实验心得**：

通过本次实验，我深刻理解了内存分配器的设计原理和性能优化方法。隐式空闲链表虽然实现简单，但性能有限，特别是在吞吐量方面。要提高性能，需要采用更复杂的数据结构，如分离空闲链表或伙伴系统。

本次实验也让我认识到，软件工程中的权衡无处不在：利用率和吞吐量、简单性和性能、正确性和效率。作为工程师，我们需要根据实际需求做出合适的选择。
