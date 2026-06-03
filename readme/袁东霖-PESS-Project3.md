# PESS-writeup

## 一、个人分工

在本次Project 3内存分配器实验中，我主要负责**allocator.c基础架构部分**的实现。具体包括：

1. **宏定义设计**：实现WSIZE/DSIZE/PACK/GET/PUT/GET_SIZE/GET_ALLOC等核心宏
2. **块指针运算宏**：实现HDRP/FTRP/NEXT_BLKP/PREV_BLKP宏
3. **堆初始化**：实现my_init()函数，创建序言块和结尾块
4. **堆扩展**：实现extend_heap()函数，扩展堆并初始化空闲块
5. **堆检查器**：实现my_check()函数，验证堆的完整性

## 二、实验方案

### 2.1 实验目标

实现一个可定制的内存分配器，替代标准的`malloc`、`free`和`realloc`函数。系统通过多个trace文件测试分配器的性能。

**评分公式**：
```
score = utilization^UTIL_WEIGHT × throughput_ratio^(1 - UTIL_WEIGHT)
```

- **UTIL_WEIGHT** = 0.50（利用率和吞吐量各占一半权重）
- **利用率** = max(最大总分配量, MEM_ALLOWANCE) / max(堆大小, MEM_ALLOWANCE)
- **吞吐量比** = min(1.0, 我们的吞吐量 / min(MAX_BASE_THROUGHPUT, LIBC_MULTIPLIER × libc吞吐量))
- **LIBC_MULTIPLIER** = 1.10（允许我们比libc慢10%）
- **MAX_BASE_THROUGHPUT** = 64000 Kops/s
- **MEM_ALLOWANCE** = 40KB

### 2.2 设计思路

经过团队讨论，我们决定采用**隐式空闲链表**方案，这是最基础但最容易实现正确的方案：

**优点**：
- 实现简单，易于理解和调试
- 不需要额外的指针存储空间
- 块的遍历通过指针算术实现，逻辑清晰

**缺点**：
- 搜索时间复杂度O(n)，吞吐量较低
- 首次适配可能导致碎片积累

**设计要点**：
- 每个块包含头部（Header）和尾部（Footer），存储块大小和分配状态
- 通过指针算术遍历所有块，无需额外的链表指针
- 使用首次适配（First Fit）搜索策略
- 支持相邻空闲块合并（Coalescing）

## 三、核心实现

### 3.1 关键宏定义设计

```c
// 基本大小定义
#define WSIZE sizeof(size_t)  // 字大小（8字节在64位系统）
#define DSIZE (2*WSIZE)       // 双字大小（16字节）
#define CHUNKSIZE (1<<12)     // 扩展堆的默认大小（4096字节）

// 打包和解包宏
#define PACK(size, alloc) ((size) | (alloc))  // 将大小和分配位打包
#define GET(p) (*(size_t*)(p))                 // 读取指针p处的值
#define PUT(p, val) (*(size_t*)(p) = (val))    // 写入值到指针p处
#define GET_SIZE(p) (GET(p) & ~0x7)           // 获取块大小（清除低3位）
#define GET_ALLOC(p) (GET(p) & 0x1)           // 获取分配位（最低位）

// 块指针运算宏
#define HDRP(bp) ((char*)(bp) - WSIZE)                              // 头部地址
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)        // 尾部地址
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))           // 下一个块
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE)) // 上一个块
```

**设计要点**：
- `WSIZE`使用`sizeof(size_t)`确保在32位和64位系统都能正确工作
- 低3位用于存储分配位和对齐填充，高位存储块大小
- `HDRP`和`FTRP`通过指针算术实现，避免额外的链表开销

### 3.2 块布局设计

```
内存布局：
+----------------+----------------+----------------+----------------+
|    Header      |     Payload    |   (Padding)    |    Footer      |
|   8 bytes      |   variable     |   optional     |   8 bytes      |
+----------------+----------------+----------------+----------------+
^                ^                                     ^
|                |                                     |
HDRP(bp)        bp                                    FTRP(bp)
```

**示例（trace_c0_v0）**：

```
3000000    # 建议堆大小
2847       # 指针ID数量
5694       # 操作数量
1          # 权重
a 0 2040   # 分配ID=0，大小2040
a 1 2040   # 分配ID=1，大小2040
a 2 48     # 分配ID=2，大小48
...
```

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

**结论**：
- 20个trace全部通过（valid=yes）
- traces目录：利用率76%，评分16.42
- additional_traces目录：利用率79%，评分20.81
- 总体平均：利用率77.5%，评分18.62

## 五、耗时表

| 阶段 | 任务内容 | 耗时 |
|------|----------|------|
| 宏定义实现 | 实现WSIZE/DSIZE/PACK/GET/PUT/HDRP/FTRP等宏 | 1.5h |
| my_init实现 | 实现堆初始化（序言块+结尾块） | 1h |
| extend_heap实现 | 实现堆扩展（覆盖结尾头部+调用coalesce） | 1.5h |
| my_check实现 | 实现堆完整性检查 | 1h |
| 测试验证 | 运行基本测试，验证功能正确性 | 0.5h |
| **总计** | | **5.5h** |

---

**实验心得**：

通过本次实验，我对内存分配器有了全面的认识：

1. **宏定义的重要性**：通过宏定义封装复杂的指针运算，使代码更清晰易读。HDRP/FTRP/NEXT_BLKP/PREV_BLKP等宏是整个分配器的基础。

2. **堆初始化的设计**：序言块和结尾块的设计简化了边界检查，避免了特殊情况的处理。这是隐式空闲链表的关键设计决策。

3. **堆扩展的实现**：extend_heap函数需要仔细处理指针计算，确保新块的头部正确覆盖旧的结尾头部。

4. **团队协作**：通过明确分工，我负责基础架构部分，其他成员负责分配逻辑、释放重分配和验证器，最终成功完成了实验任务。
