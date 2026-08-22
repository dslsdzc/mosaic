#!/usr/bin/env bash
# M5-3:26.2 原版域契约测试——真实 26.2 jar classpath → javac → java 断言。
# 退出码 0 = VANILLA CONTRACT PASSED (26.2)。
set -euo pipefail
cd "$(dirname "$0")/.."
bash ci/setup_mc_versions.sh

# 26.2 jar 为 class file 69(Java 25+),系统 JDK 21 无法加载 —— 用 25/26 JDK 跑本测试
JDK=""
for c in /usr/lib/jvm/java-26-openjdk /usr/lib/jvm/java-25-openjdk; do
  if [ -x "$c/bin/javac" ]; then JDK="$c"; break; fi
done
if [ -z "$JDK" ]; then
  echo "run_vanilla_contract_262: need JDK 25+ (jar class file 69); /usr/lib/jvm/java-2[56]-openjdk not found" >&2
  exit 3
fi

mkdir -p build/japi build/jvanilla
# 26.2 游戏 jar 不含 Mojang 运行时库(setup_mc_versions.sh 已按官方 json 下载到 libs/)
LIBS=$(ls lib/mc-versions/libs/*.jar 2>/dev/null | tr '\n' ':' | sed 's/:$//')
if [ -z "$LIBS" ]; then
  echo "run_vanilla_contract_262: 缺少 Mojang 运行时库(lib/mc-versions/libs),需联网下载" >&2
  exit 3
fi
CP="build/japi:lib/mc-versions/vanilla-26.2.jar:$LIBS"
# Bridge.java 是 Native.java 的静态委托目标(javac 编译期依赖),必须同编
"$JDK/bin/javac" -d build/japi java/mosaic/Bridge.java $(find java-api -name "*.java")
cp java-api/mosaic/vanilla/internal/*.properties build/japi/mosaic/vanilla/internal/
"$JDK/bin/javac" -cp "$CP" -d build/jvanilla \
      tests/jni/vanilla/*.java
"$JDK/bin/java" -cp "build/jvanilla:$CP" \
     VanillaContractTest Vanilla262Env
