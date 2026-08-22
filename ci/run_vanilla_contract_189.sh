#!/usr/bin/env bash
# M5-4:1.8.9 原版域契约测试——真实 1.8.9 jar(MCP 名,mcp918/temp/minecraft_rg.jar)
# + 1.8.9 启动器依赖库(~/minecraft1.8.9/mc_install/libraries:guava-17.0/commons-lang3
# 3.3.2/log4j-2.0-beta9/authlib-1.5.21 等,与 1.8.9 version json 一致;client/libs 仅有
# lwjgl/jinput,故放于回退位)→ javac → java 断言。
# 1.8.9 为 Java 8 字节码(ClassFile 52),JDK 25/26 可直接加载运行(实测,无版本专用 VM 参数)。
# 退出码 0 = VANILLA CONTRACT PASSED (1.8.9)。
set -euo pipefail
cd "$(dirname "$0")/.."
bash ci/setup_mc_versions.sh

# 与 26.2 脚本共用 JDK 探测(1.8.9 字节码在所有 JDK 可跑;取 25/26 保持双代同 JDK)
JDK=""
for c in /usr/lib/jvm/java-26-openjdk /usr/lib/jvm/java-25-openjdk; do
  if [ -x "$c/bin/javac" ]; then JDK="$c"; break; fi
done
if [ -z "$JDK" ]; then
  echo "run_vanilla_contract_189: need JDK 25+ (与 26.2 共用); /usr/lib/jvm/java-2[56]-openjdk not found" >&2
  exit 3
fi

# 1.8.9 游戏 jar:优先完全 MCP 反混淆的 client uber jar(类名+成员名均为 MCP 名,
# javap 实测 itemRegistry/getItemStackLimit/getBlockState 等);回退
# lib/mc-versions/vanilla-1.8.9.jar(= mcp918/temp/minecraft_rg.jar,Task 1 结论:
# MCP 类名 + SRG 成员名 func_/field_,成员名不可直读,仅作回退)。
JAR=""
for c in ~/minecraft1.8.9/client/build/libs/Minecraft1.8.9-Client-1.8.9-uber.jar lib/mc-versions/vanilla-1.8.9.jar; do
  if [ -f "$c" ]; then JAR="$c"; break; fi
done
if [ -z "$JAR" ]; then
  echo "run_vanilla_contract_189: 缺少 1.8.9 jar(~/.minecraft1.8.9/client/build/libs 或 lib/mc-versions/vanilla-1.8.9.jar)" >&2
  exit 3
fi

# 1.8.9 依赖库:官方启动器库树优先(mc_install/libraries 为 Maven 布局,递归收集;
# client/libs 仅有 lwjgl/jinput,缺 guava/log4j 等,作回退)
LIBS=""
for d in ~/minecraft1.8.9/mc_install/libraries ~/minecraft1.8.9/client/libs; do
  if [ -d "$d" ]; then
    for j in $(find "$d" -name "*.jar" 2>/dev/null); do
      LIBS="$LIBS:$j"
    done
  fi
done
LIBS="${LIBS#:}"
if [ -z "$LIBS" ]; then
  echo "run_vanilla_contract_189: 缺少 1.8.9 依赖库(~/minecraft1.8.9/mc_install/libraries)" >&2
  exit 3
fi

mkdir -p build/japi build/jvanilla
CP="build/japi:$JAR:$LIBS"
# Bridge.java 是 Native.java 的静态委托目标(javac 编译期依赖),必须同编
"$JDK/bin/javac" -d build/japi java/mosaic/Bridge.java $(find java-api -name "*.java")
cp java-api/mosaic/vanilla/internal/*.properties build/japi/mosaic/vanilla/internal/
"$JDK/bin/javac" -cp "$CP" -d build/jvanilla \
      tests/jni/vanilla/*.java
"$JDK/bin/java" -cp "build/jvanilla:$CP" \
     VanillaContractTest Vanilla189Env
