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

SERV_JAR=mc-server/minecraft_server.1.20.1.jar
ASM_JAR=lib/asm.jar

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
# 整声明提取:packCreate/packAddFn/packAddItem 三个声明的 ';' 在续行上
# (多行声明),按行 grep 会漏掉它们(57 声明仅见 54,F-3)——改用 perl 跨
# 行提取 "public static native" 到 ';' 的整段,段内空白归一为单空格后比对。
extract_native_decls() {
    perl -0777 -ne 'while (/public\s+static\s+native\s+[^;]*;/g) { my $m = $&; $m =~ s/\s+/ /g; $m =~ s/^ //; $m =~ s/ $//; print "$m\n"; }' "$1" | sort
}
extract_native_decls agent/mosaic/Bridge.java > build/bridge_agent_native.txt
extract_native_decls java/mosaic/Bridge.java > build/bridge_java_native.txt
# 提取完整性自检:提取数必须等于源码中 "public static native" 出现次数
# (防未来再出现异形声明使提取漏检而门禁不自知)
if [ "$(wc -l < build/bridge_agent_native.txt)" -ne "$(grep -c 'public static native' agent/mosaic/Bridge.java)" ] \
   || [ "$(wc -l < build/bridge_java_native.txt)" -ne "$(grep -c 'public static native' java/mosaic/Bridge.java)" ]; then
    echo "[agent] ERROR: native 声明提取不完整 (agent $(wc -l < build/bridge_agent_native.txt)/$(grep -c 'public static native' agent/mosaic/Bridge.java), java $(wc -l < build/bridge_java_native.txt)/$(grep -c 'public static native' java/mosaic/Bridge.java))" >&2
    exit 1
fi
if ! diff -q build/bridge_agent_native.txt build/bridge_java_native.txt >/dev/null; then
    echo "[agent] ERROR: Bridge native 声明不一致 (agent/mosaic/Bridge.java vs java/mosaic/Bridge.java):" >&2
    diff build/bridge_agent_native.txt build/bridge_java_native.txt >&2 || true
    exit 1
fi
echo "[agent] OK: Bridge native 声明一致 ($(wc -l < build/bridge_agent_native.txt) 个 native 方法)"

echo "[agent] packet map generation (M6-E: server_mappings -> PacketMap.java)..."
bash ci/gen_packet_map.sh
PACKET_MAP=build/generated/agent/com/mosaic/agent/PacketMap.java

echo "[agent] javac (classpath: asm;hooks 对 1.20.1 混淆名反射,无 MC 编译依赖)..."
# agent/mosaic/Bridge.java = M4-1 Bridge 的内嵌版(API 相同,静态块按绝对路径
# System.load)——编译进 agent jar → 运行时零外部类路径/零 java.library.path 依赖
javac -cp lib/asm.jar -d build/agentclasses \
    agent/mosaic/Bridge.java \
    agent/com/mosaic/agent/MosaicAgent.java \
    agent/com/mosaic/agent/MosaicHooks.java \
    agent/com/mosaic/agent/MosaicTransformer.java \
    "$PACKET_MAP"

echo "[agent] bundling asm classes into agent jar..."
(cd lib && unzip -qo asm.jar -d ../build/asmclasses)

echo "[agent] jar cfm (manifest + agent classes + asm classes)..."
jar cfm build/lib/mosaic-agent.jar agent/META-INF/MANIFEST.MF \
    -C build/agentclasses . -C build/asmclasses .

echo "[agent] embedding libmosaic_jni.so..."
(cd build/lib && zip -q mosaic-agent.jar libmosaic_jni.so)

# 字节码注入校验(M8-D 评审 Issue-5 固化):真实 server.jar 类字节跑真实
# transformer,ASM 扫描输出中的 invokestatic MosaicHooks.* —— 注入点签名
# 漂移(混淆名/desc 变化)立即 MISS 报错。注:transform 的 loader 参数为系统
# 加载器,帧公共父类解析回退 Object(精度下降但注入检测不受影响,输出字节
# 由 ClassReader 重读冒烟)。
echo "[agent] bytecode injection check (server.jar classes + real transformer)..."
# 1.20.1 官方 jar 是 bundler 自解压格式,真实服务端类在嵌套 jar 里
mkdir -p build/injectcheck
unzip -o -q "$SERV_JAR" 'META-INF/versions/1.20.1/server-1.20.1.jar' -d build/injectcheck
INNER_JAR=build/injectcheck/META-INF/versions/1.20.1/server-1.20.1.jar
javac -cp "$ASM_JAR":build/agentclasses -d build/injectcheck \
    ci/injectcheck/InjectCheck.java
java -cp "$ASM_JAR":build/agentclasses:build/injectcheck \
    InjectCheck "$INNER_JAR" | tee build/injectcheck/result.txt
if ! grep -q "ALL INJECTED" build/injectcheck/result.txt; then
    echo "[agent] ERROR: injection check failed (SPEC drift?)" >&2
    exit 1
fi

echo "[agent] done: build/lib/mosaic-agent.jar"
ls -la build/lib/mosaic-agent.jar
