#!/usr/bin/env bash
# 定位/下载 26.2 与 1.8.9 真实 jar,供 Provider 测试作 classpath
# M5 Task 1: 26.2 = 官方服务端 jar(mojmap 名,待 check_classnames.sh 验证);
#            1.8.9 = 用户逆向目录中 MCP 名 jar(mcp918/temp/minecraft_rg.jar,非 notch 混淆名)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p lib/mc-versions

# 26.2:从官方版本清单取 server jar(联网);失败则回退用户逆向/启动器目录的现成 jar
if URL=$(curl -s https://piston-meta.mojang.com/mc/game/version_manifest_v2.json \
    | jq -r '.versions[] | select(.id=="26.2") | .url' | head -1) \
   && [ -n "$URL" ] && [ "$URL" != "null" ] \
   && JAR_URL=$(curl -s "$URL" | jq -r '.downloads.server.url') \
   && [ -n "$JAR_URL" ] && [ "$JAR_URL" != "null" ] \
   && curl -fsSL "$JAR_URL" -o lib/mc-versions/vanilla-26.2.jar; then
  :   # 下载成功
else
  echo "26.2 官方下载失败,尝试用户逆向目录的 jar"
  find ~/minecraft26.2 -name "*.jar" -size +5M | head -1 | xargs -I{} cp {} lib/mc-versions/vanilla-26.2.jar
fi

# 26.2 bundler 内部嵌套的 game jar 才是真正的游戏类(bundler 自身只有 net/minecraft/bundler/Main)。
# 每次运行都检查:若仍是 bundler 形态,解包嵌套 jar 覆盖为目标文件(幂等)。
if [ -f lib/mc-versions/vanilla-26.2.jar ]; then
  NESTED=$(unzip -l lib/mc-versions/vanilla-26.2.jar 2>/dev/null \
    | awk '$4 ~ /^META-INF\/versions\/[^ ]+\.jar$/ {print $4}' | head -1)
  if [ -n "$NESTED" ]; then
    TMP=$(mktemp -d)
    unzip -o -q -j lib/mc-versions/vanilla-26.2.jar "$NESTED" -d "$TMP"
    mv "$TMP/$(basename "$NESTED")" lib/mc-versions/vanilla-26.2.jar
    rm -rf "$TMP"
    echo "26.2: bundler 已解包,嵌套 game jar($NESTED)就位"
  fi
fi

# 26.2 游戏 jar 是 bundler 的嵌套部分,不含 Mojang 运行时库(LogUtils/DataResult/
# authlib/brigadier/fastutil/guava 等)。按官方 version json 的 libraries.downloads
# 下载全部主构件(跳过 natives)到 libs/,供 Provider 测试作 classpath(幂等:已有即跳过)。
if [ -f lib/mc-versions/vanilla-26.2.jar ] \
   && ! ls lib/mc-versions/libs/*.jar >/dev/null 2>&1; then
  echo "26.2: 下载 Mojang 运行时库(libraries.downloads.artifact)..."
  mkdir -p lib/mc-versions/libs
  VURL=$(curl -s https://piston-meta.mojang.com/mc/game/version_manifest_v2.json \
      | jq -r '.versions[] | select(.id=="26.2") | .url' | head -1)
  if [ -n "$VURL" ] && [ "$VURL" != "null" ]; then
    python3 - "$VURL" <<'PY' | while read -r URL; do
import json, sys, urllib.request
d = json.load(urllib.request.urlopen(sys.argv[1]))
for l in d.get("libraries", []):
    a = l.get("downloads", {}).get("artifact")
    if a: print(a["url"])
PY
      [ -z "$URL" ] || curl -fsSL "$URL" -o "lib/mc-versions/libs/$(basename "$URL")" || true
    done
  fi
  if ls lib/mc-versions/libs/*.jar >/dev/null 2>&1; then
    echo "26.2: $(ls lib/mc-versions/libs/*.jar | wc -l) 个库就位"
  else
    echo "26.2: 库下载失败(无网络?),回退 ~/.minecraft/libraries(可能版本不符)" >&2
  fi
fi

# 1.8.9:优先取用户逆向目录中 MCP 反混淆命名 jar(含 MCP 类名,可作可读 classpath);
#       无则回退任意大 jar(通用 find)
if [ ! -f lib/mc-versions/vanilla-1.8.9.jar ]; then
  SRC=""
  [ -f ~/minecraft1.8.9/mcp918/temp/minecraft_rg.jar ] && SRC=~/minecraft1.8.9/mcp918/temp/minecraft_rg.jar
  if [ -z "$SRC" ]; then
    find ~/minecraft1.8.9 -name "*.jar" -size +5M | grep -iv "launchwrapper\|optifine" | head -1 \
      | xargs -I{} cp {} lib/mc-versions/vanilla-1.8.9.jar
  else
    cp "$SRC" lib/mc-versions/vanilla-1.8.9.jar
  fi
fi

ls -la lib/mc-versions/
