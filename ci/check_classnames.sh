#!/usr/bin/env bash
# 验证两代 jar 的运行时类名(26.2 是否混淆、1.8.9 是 notch 名还是 MCP 名)
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== 26.2 jar:Block/Item/Level/Entity 类名 ==="
for c in "world/level/block/Block" "world/item/Item" "world/level/Level" "world/entity/Entity" \
         "server/players/PlayerList" "nbt/CompoundTag" "commands/Commands" "network/protocol/Packet"; do
  echo -n "$c: "
  unzip -l lib/mc-versions/vanilla-26.2.jar 2>/dev/null | grep -c "net/minecraft/$c.class" || true
done

echo "=== 1.8.9 jar:Block/Item/World/Entity 类名 ==="
for c in "block/Block" "item/Item" "world/World" "entity/Entity" "nbt/NBTTagCompound" \
         "command/CommandHandler" "network/Packet" "server/MinecraftServer"; do
  echo -n "$c: "
  unzip -l lib/mc-versions/vanilla-1.8.9.jar 2>/dev/null | grep -c "net/minecraft/$c.class" || true
done

echo "=== 若 26.2 上述类全为 0,说明 jar 内是混淆名 —— 映射表需用混淆名(从 server_mappings 提取) ==="
