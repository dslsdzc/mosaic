#!/usr/bin/env bash
# M5-2:运行时域契约测试——CMake 构建 → 生成测试 pack → javac → java 断言。
# 退出码 0 = API CONTRACT TEST PASSED。
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --build build -j >/dev/null
# 生成测试 pack(1 模块 3 函数,player_join 事件,2 触发器)
build/gen_test_pack /tmp/mosaic_api_contract.pack "$PWD/build/libtest_mod.so" >/dev/null 2>&1 || \
  build/bench/gen_test_pack /tmp/mosaic_api_contract.pack "$PWD/build/libtest_mod.so"
mkdir -p build/japi
# Bridge.java 是 Native.java 的静态委托目标(javac 编译期依赖),必须同编
javac -d build/japi java/mosaic/Bridge.java java-api/mosaic/MosaicApi.java java-api/mosaic/Since.java \
      java-api/mosaic/*Exception.java $(find java-api/mosaic/runtime -name "*.java")
javac -cp build/japi -d build/japi tests/jni/ApiContractTest.java
java -Djava.library.path=build/lib -cp build/japi ApiContractTest /tmp/mosaic_api_contract.pack
