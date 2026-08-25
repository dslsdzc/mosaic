#!/usr/bin/env bash
# M6-E:1.20.1 包目录 → agent 映射表生成器。
#
# 输入:Mojang server_mappings(server.txt,1.20.1)+ src/packets.c(目录权威,
#  名字 + id)。输出:Java 静态表 PacketMap.java(混淆类名 → 目录 id,agent
#  运行时按 p.getClass().getName() 查表)。
#
# server_mappings 获取(可重入):本地 mc-server/server.txt 存在则复用;
# 否则经 piston-meta 版本清单 → 1.20.1.json → downloads.server_mappings.url
# 下载缓存。缺失且下载失败 → 报错退出并给出下载指引(不静默降级)。
#
# 校验(双端一致纪律):
#   1. 目录名 ↔ server.txt 混淆名逐项配对(要求恰好一个候选;多包同名 → 报
#      错列出候选,不猜)。
#   2. 反向覆盖:server.txt 中 net.minecraft.network.protocol.* 下全部包类
#      (除 Packet 接口与 BundleDelimiterPacket/BundlePacket 两个编码器内
#      非方向性工具类)+ 内嵌包变体(外层类 ∈ VARIANT_OUTERS,见下)必须全部
#      出现在目录中——目录漏项 → 报错。
#   3. 生成条目数 == 目录条目数(1.20.1 = 175:168 顶层包类 + 7 内嵌变体)。
#   4. 内嵌类总数守卫:server.txt 中 net.minecraft.network.protocol.** 下含
#      '$' 的类行数 == 54(1.20.1 锚点)。VARIANT_OUTERS 是白名单——未来
#      MC_VER 出现新的含 Packet 子类的外层类会被过滤静默丢弃(欠包含时反向
#      覆盖门禁不红);总数守卫让任何内嵌类景观变化(新增/删除外层变体)
#      大声失败。MC_VER 升级时此守卫必红——正是期望行为,人工确认后更新锚点。
set -euo pipefail
cd "$(dirname "$0")/.."

MC_VER=1.20.1
MAPS=mc-server/server.txt
CATALOG=src/packets.c
OUT="${1:-build/generated/agent/com/mosaic/agent/PacketMap.java}"

# ---- 1. server_mappings(缓存复用,缺则下载)----
if [ ! -f "$MAPS" ]; then
  echo "[gen_packet_map] downloading server_mappings ($MC_VER)..."
  manifest=$(curl -fsSL --max-time 60 \
      https://piston-meta.mojang.com/mc/game/version_manifest_v2.json)
  vurl=$(printf '%s' "$manifest" | python3 -c '
import json,sys
m=json.load(sys.stdin)
for v in m["versions"]:
    if v["id"]=="1.20.1": print(v["url"]); break
')
  [ -n "$vurl" ] || { echo "[gen_packet_map] ERROR: 1.20.1 not in version manifest" >&2; exit 1; }
  maps_url=$(curl -fsSL --max-time 60 "$vurl" | python3 -c '
import json,sys
print(json.load(sys.stdin)["downloads"]["server_mappings"]["url"])
')
  [ -n "$maps_url" ] || { echo "[gen_packet_map] ERROR: server_mappings url not found" >&2; exit 1; }
  curl -fsSL --max-time 120 -o "$MAPS" "$maps_url"
  echo "[gen_packet_map] downloaded $MAPS ($(wc -c < "$MAPS") bytes)"
else
  echo "[gen_packet_map] reuse $MAPS"
fi

# ---- 2. 生成 Java 映射表(校验在 python 内完成,失败即退出)----
mkdir -p "$(dirname "$OUT")"
python3 - "$MAPS" "$CATALOG" "$OUT" <<'PYEOF'
import re, sys

maps, catalog, out = sys.argv[1], sys.argv[2], sys.argv[3]

# server.txt:全部 protocol 包类 + 内嵌包变体 (fqn, obf)
# 内嵌变体判定(LC-2):内嵌类仅当外层类名 ∈ VARIANT_OUTERS 才入列。该集合
# 经 javap 全量实测核实(2026-08-25,server-1.20.1.jar):1.20.1 仅
# ServerboundMovePlayerPacket(zx)/ClientboundMoveEntityPacket(wl)两个抽象
# 包类有 Packet 子类内嵌变体(zx$a-d、wl$a-c);其余包类的内嵌类为枚举/
# 接口/记录/数据持有类(Operation/Handler/Action/Entry/Type/Stub 等,不
# extends uo=Packet),非协议包变体——全量 javap 输出摘录见
# .superpowers/sdd/task-2-report.md。外层集合与目录经反向覆盖门禁双向
# 约束:单侧增删 → 报错(与 Bundle* 排除同款纪律)。
#
# 白名单盲区(LC-2 复审):VARIANT_OUTERS 只防目录↔过滤漂移;未来新外层
# 类下的真实 Packet 子类不在白名单内 → 被静默丢弃(packet_id 保持 0)而
# 反向覆盖不红。硬守卫:下方 INNER_CLASS_TOTAL 断言 protocol.** 内嵌类
# 总数,任何内嵌类景观变化(新增/删除外层变体)大声失败——欠包含不再
# 可能静默。此数为生成期锚点(非运行期硬编码),锚定 1.20.1;
# MC_VER 升级时守卫必红,人工确认后更新锚点与 VARIANT_OUTERS。
INNER_CLASS_TOTAL = 54
inner_total = 0
VARIANT_OUTERS = {"ServerboundMovePlayerPacket", "ClientboundMoveEntityPacket"}
by_name = {}
all_packets = []
for line in open(maps):
    m = re.match(r'^(net\.minecraft\.network\.protocol\.[\w.$]+) -> ([\w$]+):$', line)
    if not m:
        continue
    fqn, obf = m.group(1), m.group(2)
    if '$' in fqn.split('.')[-1]:
        inner_total += 1                 # 内嵌类景观总数(白名单过滤前计数)
    if fqn.endswith('.Packet'):          # Packet 接口
        continue
    simple = fqn.split('.')[-1]
    if '$' in simple:
        outer = simple.split('$')[0]     # 内嵌类:外层必须是已核实变体外层
        if outer not in VARIANT_OUTERS:
            continue
    elif not simple.endswith('Packet'):
        continue
    if fqn.startswith('net.minecraft.network.protocol.Bundle'):
        continue                         # BundleDelimiterPacket/BundlePacket:
        # 编码器内非方向性工具类,不入目录(出现即 UNKNOWN)
    all_packets.append((simple, obf))
    by_name.setdefault(simple, []).append((fqn, obf))

# 内嵌类总数守卫(锚定 1.20.1,生成期锚点):总数 != 54 → 内嵌类景观变化,
# 大声失败(见上方 VARIANT_OUTERS 盲区说明)。
if inner_total != INNER_CLASS_TOTAL:
    print(f'[gen_packet_map] ERROR inner-class landscape changed: '
          f'server.txt has {inner_total} inner classes under protocol.**, '
          f'expected {INNER_CLASS_TOTAL} (1.20.1 anchor)', file=sys.stderr)
    print('  New/removed outer variant classes need review: update '
          'VARIANT_OUTERS and INNER_CLASS_TOTAL after confirmation.', file=sys.stderr)
    sys.exit(1)

# packets.c 目录 (name, id)
entries = []
for line in open(catalog):
    m = re.match(r'\s*\{\s*"([A-Za-z0-9$]+)",\s*0x([0-9A-Fa-f]+)\s*\}', line)
    if m:
        entries.append((m.group(1), int(m.group(2), 16)))

# 反向覆盖:server.txt 包类集合 == 目录名集合
map_names = {s for s, _ in all_packets}
cat_names = {n for n, _ in entries}
extra = map_names - cat_names
missing = cat_names - map_names
if extra or missing:
    print(f'[gen_packet_map] ERROR catalog/server_mappings drift:', file=sys.stderr)
    for n in sorted(extra):  print(f'  in server.txt but NOT in catalog: {n}', file=sys.stderr)
    for n in sorted(missing): print(f'  in catalog but NOT in server.txt: {n}', file=sys.stderr)
    sys.exit(1)
assert len(entries) == 175, f'catalog count != 175 (168 顶层 + 7 内嵌变体): {len(entries)}'

# 逐项配对:每目录名恰一个候选(1.20.1 无重名;多候选 → 报错不猜)
pairs = []
for name, pid in entries:
    cands = by_name.get(name, [])
    if len(cands) != 1:
        print(f'[gen_packet_map] ERROR ambiguous/missing obf for {name}: {cands}', file=sys.stderr)
        sys.exit(1)
    pairs.append((name, cands[0][1], pid))

lines = [
    'package com.mosaic.agent;',
    '',
    'import java.util.HashMap;',
    'import java.util.Map;',
    '',
    '/** 生成文件(ci/gen_packet_map.sh,勿手改):1.20.1 混淆包类名 → 包目录 id。',
    ' *  生成源 = Mojang server_mappings(server.txt,' + '1.20.1' + ')+ src/packets.c 目录;',
    ' *  校验:目录名 ↔ 混淆名逐项配对 + 反向覆盖(server.txt 全部包类 + 内嵌',
    ' *  包变体入目录)。内嵌变体键 = 混淆内嵌名(zx$a 形式),与运行时',
    ' *  getClass().getName() 对内嵌类返回的 a$b 形式一致。',
    ' *  id 语义见 include/mosaic/packets.h;未命中(非目录包/Unknown)→ 0。 */',
    'public final class PacketMap {',
    '    private PacketMap() {}',
    '',
    '    /** 混淆类全名 → 目录 id(未命中 → 查表方填 0) */',
    '    public static final Map<String, Integer> OBFS = new HashMap<>();',
    '    static {',
]
for name, obf, pid in pairs:
    lines.append(f'        OBFS.put("{obf}", 0x{pid:04X});   /* {name} */')
lines += ['    }', '}', '']
with open(out, 'w') as f:
    f.write('\n'.join(lines))
print(f'[gen_packet_map] wrote {out} ({len(pairs)} entries)')
PYEOF

# 生成产物可编译冒烟(与 build_mc_agent.sh 同编译环境)
javac -d build/generated "$OUT" && rm -rf build/generated/com
echo "[gen_packet_map] done"
