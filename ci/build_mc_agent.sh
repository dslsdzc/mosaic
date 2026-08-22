#!/usr/bin/env bash
# M4-2:构建注入 agent jar → build/lib/mosaic-agent.jar。
#
# 注入机制 = JVM 标准 Instrumentation API(javaagent + 自研 ClassFileTransformer),
# 零加载器依赖。ASM 9.7 仅作字节码编辑工具库,其类并入 agent jar
# (-C build/asmclasses .)→ 运行时零外部类路径依赖;libmosaic_jni.so 亦并入
# (zip 追加到 jar 根),premain 解出后 System.load。
set -euo pipefail
cd "$(dirname "$0")/.."

# 前置:下载产物(服务端 jar / asm.jar / world.pack)+ JNI 库
bash ci/setup_mc_server.sh
cmake --build build -j --target mosaic_jni >/dev/null

rm -rf build/agentclasses build/asmclasses build/lib/mosaic-agent.jar
mkdir -p build/agentclasses build/asmclasses build/lib

# 一致性检查:agent/mosaic/Bridge.java(M4-1 Bridge 的内嵌版)与
# java/mosaic/Bridge.java 双份并存,native 方法声明必须一致(稳定契约),
# 不一致立即报错退出。
echo "[agent] consistency check: Bridge native declarations (agent/mosaic vs java/mosaic)..."
for f in agent/mosaic/Bridge.java java/mosaic/Bridge.java; do
    if ! grep -Eq '^[[:space:]]*public static native[[:space:]]' "$f"; then
        echo "[agent] ERROR: no native declarations found in $f" >&2
        exit 1
    fi
done
grep -Eo 'public static native[^;]*;' agent/mosaic/Bridge.java | sed 's/^[[:space:]]*//' | sort > build/bridge_agent_native.txt
grep -Eo 'public static native[^;]*;' java/mosaic/Bridge.java | sed 's/^[[:space:]]*//' | sort > build/bridge_java_native.txt
if ! diff -q build/bridge_agent_native.txt build/bridge_java_native.txt >/dev/null; then
    echo "[agent] ERROR: Bridge native 声明不一致 (agent/mosaic/Bridge.java vs java/mosaic/Bridge.java):" >&2
    diff build/bridge_agent_native.txt build/bridge_java_native.txt >&2 || true
    exit 1
fi
echo "[agent] OK: Bridge native 声明一致 ($(wc -l < build/bridge_agent_native.txt) 个 native 方法)"

echo "[agent] javac (classpath: asm;hooks 对 1.20.1 混淆名反射,无 MC 编译依赖)..."
# agent/mosaic/Bridge.java = M4-1 Bridge 的内嵌版(API 相同,静态块按绝对路径
# System.load)——编译进 agent jar → 运行时零外部类路径/零 java.library.path 依赖
javac -cp lib/asm.jar -d build/agentclasses \
    agent/mosaic/Bridge.java \
    agent/com/mosaic/agent/MosaicAgent.java \
    agent/com/mosaic/agent/MosaicHooks.java \
    agent/com/mosaic/agent/MosaicTransformer.java

echo "[agent] bundling asm classes into agent jar..."
(cd lib && unzip -qo asm.jar -d ../build/asmclasses)

echo "[agent] jar cfm (manifest + agent classes + asm classes)..."
jar cfm build/lib/mosaic-agent.jar agent/META-INF/MANIFEST.MF \
    -C build/agentclasses . -C build/asmclasses .

echo "[agent] embedding libmosaic_jni.so..."
(cd build/lib && zip -q mosaic-agent.jar libmosaic_jni.so)

echo "[agent] done: build/lib/mosaic-agent.jar"
ls -la build/lib/mosaic-agent.jar
