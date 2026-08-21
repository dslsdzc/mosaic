#!/usr/bin/env bash
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

echo "=== ALL CHECKS PASSED ==="
