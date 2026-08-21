#ifndef MOSAIC_WORLD_H
#define MOSAIC_WORLD_H
#include "mosaic/base.h"

/* 世界 API(M3-2):合成世界模拟器。世界扮演"事件源":服务端逻辑(动作 + 频率
   档生成器)构造域载荷并派发到运行时 → 触发索引 → 冷/热路径。世界自身无重
   对象:玩家/实体各一张轻量表 + 单调 id 分配器,不维护方块网格(坐标任意,
   不校验存在性),不维护血量/背包/维度(均为动作签名保留参数)。

   事件名解析:动作/生成器一律经 mosaic_runtime_event_id 按名字解析 id(运行
   时事件表是构建期排序位置,世界不硬编码 id)。事件未注册 → 解析为
   MOSAIC_U32_NONE → 跳过派发(世界状态照常推进),文档化语义:世界在任何
   runtime(含无事件 pack、rt == NULL)上运行不崩、无订阅时派发计数为 0。

   确定性:全部 rng 由 world seed 驱动;同 seed + 同动作序列 + 同 step 参数
   → 同事件流(测试断言)。rng 调用序即契约,勿重排(见 src/world.c)。 */

typedef struct mosaic_world mosaic_world;
typedef struct mosaic_runtime mosaic_runtime;

/* 世界状态快照:轻量 id 分配器 + 计数(维度/区块/实体/玩家),无重对象。
   合成世界固定 1 个维度、无区块网格;x/y/z 为世界出生点(模拟锚点)。 */
typedef struct {
  u32 dimension_id;    /* 当前活动维度 id(合成世界固定 0) */
  u32 x, y, z;         /* 世界出生点坐标 */
  u32 dimension_count; /* 已加载维度数(合成世界 = 1) */
  u32 chunk_count;     /* 区块数(合成世界不维护区块网格 = 0) */
} mosaic_world_info;

/* ---- 生命周期与状态 ---- */
mosaic_world *mosaic_world_create(u32 seed);
void mosaic_world_destroy(mosaic_world *w);
mosaic_world_info mosaic_world_get_info(const mosaic_world *w);
u32 mosaic_world_ticks(const mosaic_world *w);
u32 mosaic_world_entity_count(const mosaic_world *w);
u32 mosaic_world_player_count(const mosaic_world *w);

/* ---- 世界动作(每个动作 → 对应事件类型 dispatch;rt 为运行时,载荷按事件
   类型构造,名字解析失败(NONE)→ 跳过派发) ----
   player_join:分配新 player_id(单调分配器,1 起,不复用 → "重复 join 同 id"
     不可能发生,API 亦无入参 id)并派发 player_join(载荷 mosaic_ev_player)。
     返回新 player_id(恒 ≥ 1,无失败路径)。
   player_leave:按 id 移除玩家并派发 player_leave;未知 id → no-op 无派发。
   player_chat:派发 player_chat;未知玩家 id → no-op 无派发。
   entity_spawn:分配新 entity_id + 出生点位置(rng 派生,确定性)并派发
     entity_spawn(载荷 mosaic_ev_entity);返回 entity_id(恒 ≥ 1)。
   entity_kill:按 id 移除实体并派发 entity_kill;未知 id → no-op 无派发。
   entity_damage:派发 entity_damage;未知实体 id → no-op。amount 参数:载荷
     无伤害字段(mosaic_ev_entity 不含 amount,合成世界不维护血量),仅作动作
     签名保留。
   block_break / block_place:派发对应事件(载荷 mosaic_ev_block,player_id =
     最近加入的活跃玩家,无玩家时 0)。坐标任意、不校验存在性——合成世界不
     维护方块网格;dim 参数载荷无维度字段,仅作签名保留。
   item_use / item_craft:派发对应事件(载荷 mosaic_ev_item,slot 固定 0——
     合成世界无背包模型);未知玩家 id → no-op 无派发。 */
u32 mosaic_world_player_join(mosaic_world *w, mosaic_runtime *rt);      /* 返回 player_id */
void mosaic_world_player_leave(mosaic_world *w, mosaic_runtime *rt, u32 player_id);
void mosaic_world_player_chat(mosaic_world *w, mosaic_runtime *rt, u32 player_id);
u32 mosaic_world_entity_spawn(mosaic_world *w, mosaic_runtime *rt, u32 entity_type);
void mosaic_world_entity_kill(mosaic_world *w, mosaic_runtime *rt, u32 entity_id);
void mosaic_world_entity_damage(mosaic_world *w, mosaic_runtime *rt, u32 entity_id, u32 amount);
void mosaic_world_block_break(mosaic_world *w, mosaic_runtime *rt, u32 dim, u32 x, u32 y, u32 z, u32 block_type);
void mosaic_world_block_place(mosaic_world *w, mosaic_runtime *rt, u32 dim, u32 x, u32 y, u32 z, u32 block_type);
void mosaic_world_item_use(mosaic_world *w, mosaic_runtime *rt, u32 player_id, u32 item_id);
void mosaic_world_item_craft(mosaic_world *w, mosaic_runtime *rt, u32 player_id, u32 item_id);

/* 世界步进:推进 n_ticks(ticks 计数 +n),每 tick 按频率档生成事件并派发
   (生成器说明见 src/world.c;LOW 档含 world_step 开始时的自动补玩家——0 玩家
   强制 join 1,保持世界活跃)。返回派发总数(所有派发中成功执行的订阅调用数
   之和;无订阅/事件未注册时为 0,世界仍推进)。 */
u64 mosaic_world_step(mosaic_world *w, mosaic_runtime *rt, u32 n_ticks);

#endif
