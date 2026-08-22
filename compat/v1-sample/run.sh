#!/usr/bin/env bash
# M5-5:v1 API 兼容样例——自包含编译 + 运行。
# 只使用 API_VERSION 1 引入的成员;v1 签名被删/改 → 本脚本编译失败 → 门禁红。
set -euo pipefail
cd "$(dirname "$0")/../.."
build/gen_test_pack /tmp/mosaic_v1_sample.pack "$PWD/build/libtest_mod.so" >/dev/null 2>&1 || \
  build/bench/gen_test_pack /tmp/mosaic_v1_sample.pack "$PWD/build/libtest_mod.so"
mkdir -p build/japi build/jcompat
# Bridge.java 是 Native.java 的静态委托目标(javac 编译期依赖),必须同编(与 ci/run_vanilla_contract_*.sh 一致)
javac -d build/japi java/mosaic/Bridge.java $(find java-api -name "*.java")
javac -cp build/japi -d build/jcompat compat/v1-sample/src/v1sample/V1SampleMod.java
java -Djava.library.path=build/lib -cp build/japi:build/jcompat v1sample.V1SampleMod /tmp/mosaic_v1_sample.pack
