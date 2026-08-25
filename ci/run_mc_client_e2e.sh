#!/usr/bin/env bash
# Task 2:play 阶段最小客户端 E2E(1.20.1 + agent + bench/mc_client/McClient.java)
#
# 编排:起服(agent + world.pack)→ 等 Done → RCON 基线 /mosaic status →
#       客户端(握手→登录→play:KeepAlive 回发/传送确认/聊天命令/聊天消息/
#       /summon 实体)→ RCON 终态 /mosaic status → 停服(graceful stop →
#       SIGKILL 收尾,块保存慢为已知环境问题)→ 证据核对(缺失 → 非零退出)。
#
# 环境约束:
#   - online-mode=false(本地无 Mojang 认证;setup_mc_server.sh 已写)
#   - enforce-secure-profile=false(最小客户端不实现签名聊天链;记录于报告)
#   - world 目录复用(首轮生成慢;spawn 区块常驻保证 /summon 落点已加载)
#   - 幂等:杀残留服务端、清 console.log、world 不删
#   - 客户端不进 gates.sh(需真实 1.20.1 服务端,CI 无此环境;报告说明)
set -euo pipefail
cd "$(dirname "$0")/.."

REPO="$(pwd)"
SRV=mc-server
LOG="$REPO/$SRV/console.log"
CLIENT_LOG="$REPO/bench-out/mc_client_e2e_client.log"
PID_FILE="$REPO/bench-out/mc_server.pid"
USER=MosaicBot
HOST=127.0.0.1
PORT=25565
START_WAIT=300

mkdir -p bench-out build/mc_client

# ---- 0. server.properties(离线模式 + 不强制签名聊天) ----
for kv in "online-mode=false" "enforce-secure-profile=false"; do
  grep -qF "$kv" "$SRV/server.properties" || echo "$kv" >> "$SRV/server.properties"
done

# ---- 1. 构建(agent + 客户端) ----
bash ci/build_mc_agent.sh >/dev/null
javac -d build/mc_client bench/mc_client/McClient.java

# ---- 2. 幂等:杀残留服务端、清日志 ----
pkill -f "minecraft_server.1.20.1.jar" 2>/dev/null || true
sleep 2
: > "$LOG"

# ---- 3. 起服(后台;输出 → console.log;exec 使 $! = java 进程本身) ----
# Task 3:-Dmosaic.listen=packet_received,packet_sent → agent 启动时注册内置
# 事件监听器(Java 观测通道),每包派发返回后打印 LISTENER 行(证据 §8)
( cd "$REPO/$SRV" && exec java -javaagent:../build/lib/mosaic-agent.jar \
    -Dmosaic.listen=packet_received,packet_sent -Xmx1G \
    -jar minecraft_server.1.20.1.jar nogui ) > "$LOG" 2>&1 &
echo $! > "$PID_FILE"
for i in $(seq 1 $START_WAIT); do
  grep -q "Done (" "$LOG" && break
  grep -qa "Failed to start the minecraft server" "$LOG" && { echo "FATAL: server failed to start"; tail -20 "$LOG"; exit 1; }
  sleep 1
done
grep -q "Done (" "$LOG" || { echo "FATAL: server did not finish starting"; tail -30 "$LOG"; exit 1; }
echo "[e2e] server up (pid $(cat $PID_FILE))"

# ---- RCON 助手(最小 python 客户端;认证 + 执行命令) ----
rcon() {
  python3 - "$1" <<'PY'
import socket, struct, sys
cmd = sys.argv[1]
s = socket.create_connection(("127.0.0.1", 25575), timeout=5)
s.settimeout(5)
def send(i, t, payload):
    body = struct.pack("<ii", i, t) + payload.encode("utf-8") + b"\x00\x00"
    s.sendall(struct.pack("<i", len(body)) + body)
def recv_exact(n):
    data = b""
    while len(data) < n:
        chunk = s.recv(n - len(data))
        if not chunk: raise EOFError()
        data += chunk
    return data
def recv_pkt():
    ln = struct.unpack("<i", recv_exact(4))[0]
    data = recv_exact(ln)
    return data[8:-2].decode("utf-8", "replace")
send(1, 3, "mosaic")            # SERVERDATA_AUTH
try:
    recv_pkt()
    send(2, 2, cmd)              # SERVERDATA_EXECCOMMAND
    print(recv_pkt())
except Exception:
    pass
s.close()
PY
}

# ---- 4. RCON 基线(客户端连接前;计数应从 0 起) ----
rcon "mosaic status" >/dev/null 2>&1 || true

# ---- 5. 客户端动作序列(握手→登录→play;详见 McClient.java 头注释) ----
set +e
timeout 60 java -cp build/mc_client McClient "$HOST" "$PORT" "$USER" > "$CLIENT_LOG" 2>&1
RC=$?
set -e
echo "[e2e] client exit=$RC"

# ---- 6. RCON 终态(客户端动作后) ----
rcon "mosaic status" >/dev/null 2>&1 || true

# ---- 7. 停服(graceful stop → SIGKILL 收尾;块保存慢是已知环境问题) ----
rcon "stop" >/dev/null 2>&1 || true
for i in $(seq 1 45); do
  kill -0 "$(cat $PID_FILE)" 2>/dev/null || break
  sleep 1
done
if kill -0 "$(cat $PID_FILE)" 2>/dev/null; then
  echo "[e2e] stop slow, SIGKILL"
  kill -9 "$(cat $PID_FILE)" 2>/dev/null || true
fi
rm -f "$PID_FILE"

# ---- 8. 证据核对(缺失 → 非零退出) ----
fail=0
check() { # $1 = 描述, $2 = grep 模式
  local desc=$1; shift
  if grep -qaE "$1" "$LOG"; then echo "[e2e] OK   $desc"; else echo "[e2e] MISS $desc"; fail=1; fi
}
check "player join (vanilla log line)" "logged in with entity id"
check "agent status printed" "Mosaic agent: status functions="
check "player_join calls>=1" "player_join event_id=[0-9]+ calls=[1-9]"
check "player_command calls>=1" "player_command event_id=[0-9]+ calls=[1-9]"
check "player_chat calls>=1" "player_chat event_id=[0-9]+ calls=[1-9]"
check "entity_spawn calls>=1" "entity_spawn event_id=[0-9]+ calls=[1-9]"
check "packet_received calls>=1" "packet_received event_id=[0-9]+ calls=[1-9]"
check "packet_sent calls>=1" "packet_sent event_id=[0-9]+ calls=[1-9]"
# Task 3:事件监听器(Java 观测通道)证据——agent 启动注册 → 真实 play 包
# 派发返回后广播:LISTENER 行存在、载荷 12B(24 hex)、至少一条已知包载荷
# (ChatMessage 0x0105 + 消息 size 51 = 0x33;player_id = 运行时实体 id(进程内
# 静态计数,不持久),world 复用重跑时玩家 id ≥ 2——门禁通配该字段,只锁
# 与世界状态无关的确定性部分:packet_id 0x0105 + size 51)
check "listener registered at startup" "listener registered for packet_received"
check "listener packet_received broadcast" "LISTENER packet_received executed=[1-9][0-9]* payload=[0-9a-f]{24}"
check "listener packet_sent broadcast" "LISTENER packet_sent executed=[1-9][0-9]* payload=[0-9a-f]{24}"
check "listener known payload (ChatMessage 0x0105 size=51)" \
  "LISTENER packet_received executed=[1-9][0-9]* payload=[0-9a-f]{8}0501000033000000"
check "vanilla chat broadcast" "<$USER> hello from mosaic-client"
check "chat_message extraction" "chat_message=.*hello from mosaic-client"
grep -qa "\[PLAY\] entered play stage" "$CLIENT_LOG" || { echo "[e2e] MISS client reached play"; fail=1; }
grep -qa "\[SEND\] ChatCommand" "$CLIENT_LOG" || { echo "[e2e] MISS client chat command sent"; fail=1; }
grep -qa "\[SEND\] ChatMessage" "$CLIENT_LOG" || { echo "[e2e] MISS client chat message sent"; fail=1; }
grep -qa "\[RECV\] PlayerPositionAndLook" "$CLIENT_LOG" || { echo "[e2e] MISS position recv"; fail=1; }
# KeepAlive 回显(评审修复:客户端存活 20s > 服务端 15s keepalive 间隔,
# 首个 keepalive 到达并回发必须真实发生——此前 8.5s 断连从未行使此路径)
grep -qa "\[RECV\] KeepAlive" "$CLIENT_LOG" || { echo "[e2e] MISS keepalive recv (server->client)"; fail=1; }
grep -qa "\[SEND\] KeepAlive" "$CLIENT_LOG" || { echo "[e2e] MISS keepalive echo (client->server)"; fail=1; }
[ "$RC" -eq 0 ] || { echo "[e2e] MISS client exit code (got $RC)"; fail=1; }

echo
echo "=== E2E evidence: server console (agent status / join / chat lines) ==="
grep -aE "logged in with entity id|UUID of player|lost connection" "$LOG" || true
grep -aE "Mosaic agent: (status|  (player_|packet_|entity_|chat_message|server_|block_|tick))" "$LOG" | tail -40 || true
grep -aE "<$USER> |chat_message=" "$LOG" || true
echo
echo "=== E2E evidence: event listener channel (Task 3; registration + samples + counts) ==="
grep -aE "Mosaic agent: (listener registered|LISTENER)" "$LOG" | head -8 || true
grep -aE "LISTENER packet_received" "$LOG" | wc -l | sed 's/^/listener packet_received lines: /'
grep -aE "LISTENER packet_sent" "$LOG" | wc -l | sed 's/^/listener packet_sent lines: /'
echo
echo "=== E2E evidence: client log (hashes / login / actions / frames) ==="
grep -aE "\[(HASH|LOGIN|PLAY|CLIENT)\]" "$CLIENT_LOG" | head -30 || true
echo "--- SEND ---"
grep -aE "\[SEND\]" "$CLIENT_LOG" | head -30 || true
echo "--- RECV (key packets) ---"
grep -aE "\[RECV\] (KeepAlive|PlayerPositionAndLook|SpawnEntity|Disconnect|SystemChat)" "$CLIENT_LOG" | head -30 || true
echo
if [ $fail -eq 0 ]; then
  echo "=== E2E ALL EVIDENCE OK ==="
else
  echo "=== E2E EVIDENCE MISSING ==="
  exit 1
fi
