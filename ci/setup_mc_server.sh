#!/usr/bin/env bash
# M4-2:1.20.1 服务端环境准备(可重入):
#   - 查 version_manifest_v2.json 取 1.20.1 downloads.server.url → mc-server/minecraft_server.1.20.1.jar
#   - ASM 9.7 核心库 → lib/asm.jar(字节码编辑工具库,非加载器;agent jar 构建时并入)
#   - mc-server/eula.txt(eula=true)、server.properties(offline、view-distance=4、max-players=10)
#   - 构建 gen_world_pack 并生成 mc-server/packs/world.pack(事件集与 hooks 派发名一致)
# 下载产物已存在则跳过;eula/properties/pack 每次重写(可再生成)。
set -euo pipefail
cd "$(dirname "$0")/.."

MC_VER=1.20.1
SERV_JAR=mc-server/minecraft_server.${MC_VER}.jar
ASM_JAR=lib/asm.jar

mkdir -p mc-server/packs lib

# ---- 1. 服务端 jar(从官方版本清单解析下载 URL;已存在则跳过)----
if [ ! -f "$SERV_JAR" ]; then
  echo "[mc] downloading minecraft_server.${MC_VER}.jar (~48MB)..."
  manifest=$(curl -fsSL --max-time 60 https://piston-meta.mojang.com/mc/game/version_manifest_v2.json)
  vurl=$(printf '%s' "$manifest" \
    | grep -o '"id"[[:space:]]*:[[:space:]]*"'"$MC_VER"'"[^}]*' \
    | grep -o 'https://piston-meta.mojang.com/v1/packages/[^"]*' | head -1)
  [ -n "$vurl" ] || { echo "[mc] ERROR: ${MC_VER} not found in version manifest" >&2; exit 1; }
  srv_url=$(curl -fsSL --max-time 60 "$vurl" \
    | grep -o '"server"[[:space:]]*:[[:space:]]*{[^}]*}' \
    | grep -o 'https://[^"]*')
  [ -n "$srv_url" ] || { echo "[mc] ERROR: server download url not found" >&2; exit 1; }
  curl -fsSL --max-time 300 -o "$SERV_JAR" "$srv_url"
  echo "[mc] downloaded $SERV_JAR"
else
  echo "[mc] $SERV_JAR exists, skip download"
fi

# ---- 2. ASM 核心库(已存在则跳过)----
if [ ! -f "$ASM_JAR" ]; then
  echo "[mc] downloading asm-9.7..."
  curl -fsSL --max-time 120 -o "$ASM_JAR" \
    https://repo1.maven.org/maven2/org/ow2/asm/asm/9.7/asm-9.7.jar
  echo "[mc] downloaded $ASM_JAR"
else
  echo "[mc] $ASM_JAR exists, skip download"
fi

# ---- 3. eula / server.properties ----
cat > mc-server/eula.txt <<'EOF'
eula=true
EOF
cat > mc-server/server.properties <<'EOF'
online-mode=false
view-distance=4
max-players=10
server-port=25565
level-name=world
spawn-protection=0
enable-rcon=true
rcon.port=25575
rcon.password=mosaic
# -Xmx1G 下首轮世界生成慢,禁用 60s 看门狗(避免误杀;本注入验证环境)
max-tick-time=-1
EOF
echo "[mc] wrote mc-server/eula.txt + server.properties"

# ---- 4. 世界 pack(事件集与 agent hooks 派发名一致)----
echo "[mc] building gen_world_pack..."
cmake -B build >/dev/null
cmake --build build -j --target gen_world_pack >/dev/null
if [ ! -f build/libtest_mod.so ]; then
  cmake --build build -j --target test_mod >/dev/null
fi
echo "[mc] generating mc-server/packs/world.pack..."
build/gen_world_pack mc-server/packs/world.pack "$PWD/build/libtest_mod.so"

echo "[mc] setup done: $SERV_JAR, $ASM_JAR, mc-server/packs/world.pack"
