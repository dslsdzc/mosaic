#ifndef MOSAIC_EVENTS_H
#define MOSAIC_EVENTS_H
#include "mosaic/base.h"

/* 事件类型 API v1(设计规格第 7 节触发索引 / 第 18 节 Event API)。
   事件类型本身就是公开 API:名字 + 载荷签名 + 语义。订阅声明在构建期静态进
   触发索引(本头不提供运行期注册机制),派发核心保持小。

   目录覆盖真实生态(Bukkit/Paper ~250 事件、NeoForge 数百到上千)的域分布,
   MOSAIC_MAX_EVENTS 4096 与此规模匹配(名称表二分查找,上限放宽无性能影响)。 */

/* ---- 载荷结构体(按域对齐 4 字节;u32 字段天然 4 对齐) ----
   每个域一组事件共享同一载荷签名;触发时由事件源按域结构体填充。 */

typedef struct { u32 player_id; } mosaic_ev_player;   /* 玩家域:加入/离开/重生/死亡/聊天 */

typedef struct { u32 player_id; u32 cmd_hash; } mosaic_ev_player_command;
  /* 玩家域:命令(player_id + FNV-1a-32(命令文本去前导 '/')哈希,确定性可复核) */

typedef struct { u32 player_id; u32 x, y, z; u32 block_type; } mosaic_ev_block;
  /* 方块域:破坏/放置/交互/计划刻 */

typedef struct { u32 player_id; u32 item_id; u32 slot; } mosaic_ev_item;
  /* 物品域:使用/合成/拾取/丢弃/背包变化 */

typedef struct { u32 player_id; u32 packet_id; u32 size_hint; } mosaic_ev_network;
  /* 网络域:packet_received/packet_sent(玩家连接的入站/出站包;player_id =
     连接玩家,非游戏阶段(登录/状态/握手)→ 0;packet_id = 包目录 id
     (include/mosaic/packets.h),未命中目录 → 0(UNKNOWN);size_hint = 包
     大小近似值,v1 恒 0——1.20.1 钩子点(channelRead0/doSendPacket 入口)
     无包字节数可得,留待后续版本,文档注明) */

typedef struct { u32 entity_id; u32 entity_type; u32 x, y, z;
                 u32 dimension; u32 source; } mosaic_ev_entity;
  /* 实体域:生成/死亡/受伤/交互/刻(尾部 dimension = FNV-1a-32(维度 location
     串)、source = 生成来源——append-only 扩展,既有字段不动) */

typedef struct { u32 tick_no; } mosaic_ev_tick;       /* 世界周期:刻/保存/加载/昼夜/天气 */

typedef struct { } mosaic_ev_empty;                    /* 服务域:启动/停止(无载荷) */

/* ---- 事件目录 ----
   语义注释(按域;命名沿用真实生态:player_join/player_leave/player_respawn/
   player_death/player_chat/player_command/player_move/player_swing_arm/
   player_bucket_fill/player_bucket_empty/player_bed_enter/player_bed_leave/…,
   block_break/block_place/block_interact/block_tick/block_explode/block_burn/
   block_redstone/…, item_use/item_craft/item_pickup/item_drop/item_smelt/
   inventory_change/…, entity_spawn/entity_death/entity_damage/entity_interact/
   entity_tick/entity_explode/entity_tame/…, tick/world_save/world_load/
   time_change/weather_change/chunk_load/chunk_unload/dimension_load/
   dimension_unload/…, server_start/server_stop/…):

   - player_*     :玩家生命周期与交互。join/leave 在进出服务器时触发(载荷
                    player_id);respawn 在死亡后重生时;death 在玩家死亡时;
                    chat/command 在发言/执行命令时;move 每移动触发(高频);
                    swing_arm 挥动手臂;bucket_fill/bucket_empty 使用桶;
                    bed_enter/bed_leave 进入/离开床。
   - block_*      :方块生命周期。break/place 在玩家破坏/放置方块时(载荷含
                    坐标与方块类型);interact 玩家右键方块;tick 方块计划刻
                    (高频);explode 方块被爆炸破坏;burn 着火蔓延;redstone
                    红石信号变化;piston_extend/retract 活塞推动/收回。
   - item_*       :物品生命周期与背包。use 使用物品;craft 合成;pickup/drop
                    拾取/丢弃;smelt 熔炉烧炼;inventory_change 任意背包变化。
   - packet_*     :网络域。packet_received/packet_sent 在连接收/发包时触发
                    (载荷 player_id + packet_id + size_hint;非游戏阶段
                    player_id=0,size_hint v1 恒 0)。
   - entity_*     :实体生命周期。spawn/death 生成/死亡;damage 受伤(含
                    damage_by_entity/damage_by_block 成因变体);interact 玩家
                    与实体交互;tick 每刻(高频);explode 爆炸;tame 驯服。
   - world 周期   :tick 世界刻(最高频);world_save/load 世界保存/加载;
                    time_change 昼夜;weather_change 天气;chunk_load/unload
                    区块加载/卸载;dimension_load/unload 维度加载/卸载。
   - server_*     :服务生命周期,无载荷(server_start/server_stop 等)。

   freq 频率档决定合成世界生成器的事件分布(LOW = 稀有生命周期事件,
   MID = 常规, HIGH = 每刻/高吞吐)。 */

typedef enum {
  MOSAIC_EV_FREQ_LOW = 0, MOSAIC_EV_FREQ_MID, MOSAIC_EV_FREQ_HIGH
} mosaic_ev_freq;

typedef struct {
  const char *name;        /* 事件名,小写蛇形,目录内唯一 */
  mosaic_ev_freq freq;     /* 频率档:决定合成世界生成器分布 */
  u32 payload_size;        /* 载荷结构体大小(0 = 无载荷) */
} mosaic_ev_spec;

/* 事件目录:按名字升序(长度感知序——与 builder 事件名排序/运行时二分同一
   语义;名字唯一故与 strcmp 序一致)。实现见 src/events.c */
extern const mosaic_ev_spec mosaic_events_catalog[];
extern const u32 mosaic_events_catalog_count;

/* 目录内二分查找(与 builder 排序同序)。未命中/空名字 → NULL。 */
const mosaic_ev_spec *mosaic_event_spec_by_name(const char *name);

/* M6-D N2 目录访问器(跨语言一致性门禁用):返回第 index 个目录名(目录内
   静态字符串,与 mosaic_events_catalog 同序);越界 → NULL。Java 契约测试
   经 JNI 遍历全部名字与 EVENT_NAMES 常量表逐项比对——events.c 增删目录名
   → Java 比对失败 → 测试红,防目录跨语言漂移。 */
const char *mosaic_event_catalog_name(u32 index);

#endif
