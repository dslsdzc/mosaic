#!/usr/bin/env bash
# 前置条件:需要 JDK(CMake FindJNI,见 README);无 JDK 环境请跳过或先安装。
# Mosaic M1 验收门禁:Release 构建 + 单元/属性测试 + 10M 基准硬指标
# 任何一步失败 → 非零退出(回归即失败)
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo "=== ctest ==="
ctest --test-dir build --output-on-failure

echo "=== gates (10M cold functions) ==="
build/bench/bench_runner 10000000

echo "=== gates (100M sharded: 100 shards x 1M) ==="
build/bench/bench_runner 100000000 2>/dev/null "" "" "" 100

echo "=== world scenarios (100k fns, 10 entities, 1000 ticks) ==="
build/bench/world_bench

echo "=== vanilla provider contracts (dual-generation: 26.2 + 1.8.9) ==="
bash ci/run_vanilla_contract_262.sh
bash ci/run_vanilla_contract_189.sh

echo "=== ALL CHECKS PASSED ==="
