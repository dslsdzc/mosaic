#!/usr/bin/env bash
# M4-1:JVM Bridge 测试——CMake 构建 → 生成测试 pack → javac → java 断言。
# 前置:JDK 21(CMake FindJNI 自动探测;JAVA_HOME 未设时用 /usr/lib/jvm/default)。
# 退出码 0 = 全部断言通过。
set -euo pipefail
cd "$(dirname "$0")/.."

echo "[jni] cmake build..."
cmake -B build >/dev/null
cmake --build build -j >/dev/null

echo "[jni] generate test packs..."
build/gen_test_pack build/jni_test.pack build/bench/synth_mod.so
build/gen_test_pack build/jni_add.pack build/bench/synth_mod.so add

echo "[jni] javac..."
rm -rf build/javaclasses
mkdir -p build/javaclasses
javac -d build/javaclasses java/mosaic/Bridge.java tests/jni/MosaicBridgeTest.java

echo "[jni] java (java.library.path=build/lib)..."
java -Djava.library.path=build/lib -cp build/javaclasses \
     mosaic.jni.MosaicBridgeTest build/jni_test.pack build/jni_add.pack

echo "[jni] OK"
