#!/usr/bin/env bash
# 定位/下载 26.2 与 1.8.9 真实 jar,供 Provider 测试作 classpath
# M5 Task 1: 26.2 = 官方服务端 jar(mojmap 名,待 check_classnames.sh 验证);
#            1.8.9 = 用户逆向目录中 MCP 名 jar(mcp918/temp/minecraft_rg.jar,非 notch 混淆名)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p lib/mc-versions

# 26.2:从官方版本清单取 server jar(联网);失败则回退用户逆向/启动器目录的现成 jar
if [ ! -f lib/mc-versions/vanilla-26.2.jar ]; then
  URL=$(curl -s https://piston-meta.mojang.com/mc/game/version_manifest_v2.json \
    | jq -r '.versions[] | select(.id=="26.2") | .url' | head -1)
  if [ -n "$URL" ] && [ "$URL" != "null" ]; then
    JAR_URL=$(curl -s "$URL" | jq -r '.downloads.server.url')
    curl -sL "$JAR_URL" -o lib/mc-versions/vanilla-26.2.jar
  fi
  # 下载失败(空文件/过小)则回退本地现成 jar
  if [ ! -f lib/mc-versions/vanilla-26.2.jar ] \
     || [ "$(stat -c%s lib/mc-versions/vanilla-26.2.jar 2>/dev/null || echo 0)" -lt 5000000 ]; then
    rm -f lib/mc-versions/vanilla-26.2.jar
    echo "26.2 官方下载失败,尝试用户逆向目录的 jar"
    SRC=$(find ~/minecraft26.2 -name "*.jar" -size +5M 2>/dev/null | head -1)
    [ -z "$SRC" ] && SRC=$(find ~/.minecraft/versions -maxdepth 2 -name "26.2*.jar" -size +5M 2>/dev/null \
      | grep -iv "fabric\|neoforge\|forge" | head -1)   # 优先非 modded 的 26.2 jar
    [ -z "$SRC" ] && SRC=$(find ~/.minecraft/versions -maxdepth 2 -name "26.2*.jar" -size +5M 2>/dev/null | head -1)
    if [ -n "$SRC" ]; then cp "$SRC" lib/mc-versions/vanilla-26.2.jar; else echo "26.2 jar 获取失败(网络+本地均不可用)"; fi
  fi
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
