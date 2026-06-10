#!/usr/bin/env python3
import opentuner
from opentuner import ConfigurationManipulator
from opentuner import IntegerParameter
from opentuner.search.objective import MaximizeMetric
import subprocess
import re

class AllocatorTuner(opentuner.MeasurementInterface):
    def manipulator(self):
        manipulator = ConfigurationManipulator()
        # 搜索空间定义：寻找最佳的扩展块大小（1024 字节到 16384 字节）
        # 如果你们后续想调优其他常量，比如分离链表的阶层边界，也可以在这里加参数
        manipulator.add_parameter(IntegerParameter('CHUNKSIZE', 1024, 16384))
        return manipulator

    def run(self, desired_result, input, limit):
        cfg = desired_result.configuration.data
        chunk_size = cfg['CHUNKSIZE']

        # 1. 动态注入参数并重新编译
        # 使用 -D 宏定义将 Python 中的参数传给 C 编译器
        compile_cmd = f"make clean && make CFLAGS='-Wall -Wextra -Werror -O3 -DCHUNKSIZE={chunk_size}'"
        compile_process = subprocess.run(
            compile_cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        
        # 编译失败直接给 0 分
        if compile_process.returncode != 0:
            return opentuner.Result(time=0, accuracy=0.0)

        # 2. 运行所有 trace 并提取性能得分
        # 注意：这里假设你们的 mdriver 会在最后输出类似 "Perf index = 85.5" 的字样
        run_cmd = "./mdriver"
        run_process = subprocess.run(
            run_cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        perf_index = 0.0
        # 用正则匹配出加权后的性能总分 (P)
        # 匹配模式可能需要根据你们 mdriver 的实际输出做微调，通常是 "Perf index = XX" 或类似格式
        match = re.search(r'(?i)perf index\s*=\s*(\d+\.\d+)', run_process.stdout)
        if match:
            perf_index = float(match.group(1))

        # 3. 将分数反馈给算法 (由于 objective 设置了 MaximizeMetric，分数越高越好)
        return opentuner.Result(time=0, accuracy=perf_index)

if __name__ == '__main__':
    # 启动调优器
    argparser = opentuner.default_argparser()
    AllocatorTuner.main(argparser)