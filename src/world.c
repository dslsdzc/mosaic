/* src/world.c — M3-2:合成世界模拟器。世界扮演"事件源":服务端逻辑(动作 +
   频率档生成器)构造域载荷并派发到运行时 → 触发索引 → 冷/热路径。世界无重
   对象:玩家/实体各一张轻量表 + 单调 id 分配器;不维护方块网格(坐标任意,
   不校验存在性)。全部 rng 由 world seed 驱动(确定性:同 seed + 同动作序列
   + 同 step 参数 → 同事件流;rng 调用序即契约,勿重排)。 */
#include "mosaic/world.h"
#include "mosaic/events.h"
#include "mosaic/event.h"
#include "mosaic/runtime.h"
#include <stdlib.h>
#include <string.h>

/* ---- 频率档生成器(每 tick 序列) ----
   速率模型以覆盖表 g_freq_fixes 为唯一来源:世界可派发的全部事件(20 个)按
   (生成档位, 抽样概率 num/den, 载荷构造方式)入表并注明理由;step 按表驱动
   LOW/MID 两轮抽样(roll 序 = 表序,即 rng 调用序契约,勿重排),HIGH 档每
   tick 全量派发(不入抽样轮,实现见 step 中 HIGH 块,表中标注)。events.h
   目录 freq 是 API 语义档(供消费方参考:事件在真实生态中的常见程度),生成
   器速率模型独立于目录档位——两者可独立演化,偏离理由见表内注释。
   a) HIGH 档(每 tick 全量,不入抽样轮):entity_tick 对每个存活实体派发 1
      次(载荷 {entity_id, type, 位置;位置每 tick 随机游走 ±1,确定性})+
      tick 派发 1 次(载荷 {tick_no})
   b) LOW 档:每 tick 每事件独立抽样(概率见表,多为 0.1%;entity_spawn
      0.2%);每个事件都掷骰后按需派发(rng 调用序与状态无关,确定性;缺玩
      家/实体时跳过派发——骰已掷)
   c) MID 档:每 tick 5% 每事件独立抽样
   d) fire-and-forget 不对称(自洽设计):生成器派发 player_leave/entity_death
      只发事件、不更新世界表(动作 leave/kill 才真正删行)——对应真实生态中
      事件先于状态收敛的语义;因此实体数净增(生成 0.2% > 击杀 0.1%)、玩家
      数缓慢漂升(join 0.1% + 自动补玩家,leave 不删行)
   e) world_step 开始时若玩家数 < 3 且概率命中 → 自动补玩家(玩家数 0 时强
      制 join 1——硬存活保证,保持世界活跃;此机制是活跃度保证,不入抽样表) */

typedef struct { u32 id, type, x, y, z; } world_entity;
typedef struct { u32 id; } world_player;

struct mosaic_world {
  u32 seed;
  u64 rng;
  u32 ticks;
  u32 next_player_id;      /* 单调分配器:1 起,不复用 → 重复 join 同 id 不可能 */
  u32 next_entity_id;
  u32 last_join_player;    /* 最近加入的活跃玩家(方块事件归属;0 = 无) */
  world_player *players; u32 n_players, cap_players;
  world_entity *entities; u32 n_entities, cap_entities;
};

#define WORLD_DIM_ID 0u     /* 合成世界固定单维度 */
#define WORLD_SPAWN_Y 64u   /* 出生点 y 锚点(方块高度 64) */

/* ---- 确定性 rng:xorshift64*;无浮点、无 libc rand,跨平台同流 ---- */
static u64 rng_seed_mix(u32 seed) {
  u64 z = (u64)seed + 0x9E3779B97F4A7C15ull;   /* splitmix64 扩散 */
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static u64 rng_next(mosaic_world *w) {
  u64 x = w->rng;
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
  w->rng = x;
  return x * 0x2545F4914F6CDD1Dull;
}
static u32 rng_below(mosaic_world *w, u32 n) {
  return n ? (u32)(rng_next(w) % n) : 0u;
}
static int rng_chance(mosaic_world *w, u32 num, u32 den) {
  return den ? rng_below(w, den) < num : 0;
}

/* ---- 载荷暂存缓冲:动作/生成器把域载荷构造进 64B 零化暂存区再派发。
   契约:订阅代码可从 event 指针安全读取 ≤56B(test_mod code_off 5 载荷探针
   memcpy 56B);域载荷结构体 ≤20B,暂存区其余字节为零。 ---- */
typedef union {
  u8 bytes[64];
  mosaic_ev_player p; mosaic_ev_block b; mosaic_ev_item i; mosaic_ev_entity e; mosaic_ev_tick t;
} world_stage;

/* 事件名 → id 运行时解析(构建期排序位置,不硬编码);未注册 → NONE。 */
static u32 world_ev_id(mosaic_runtime *rt, const char *name) {
  return rt ? mosaic_runtime_event_id(rt, name) : MOSAIC_U32_NONE;
}
/* 事件未注册(NONE)→ 跳过派发(文档化:世界仍推进)。 */
static u64 world_dispatch(mosaic_runtime *rt, u32 event_id, const void *payload) {
  if (!rt || event_id == MOSAIC_U32_NONE) return 0;
  return mosaic_event_dispatch(rt, event_id, payload);
}

static int world_player_known(const mosaic_world *w, u32 pid) {
  for (u32 i = 0; i < w->n_players; i++)
    if (w->players[i].id == pid) return 1;
  return 0;
}
static int world_player_index(const mosaic_world *w, u32 pid) {
  for (u32 i = 0; i < w->n_players; i++)
    if (w->players[i].id == pid) return (int)i;
  return -1;
}
static world_entity *world_entity_find(mosaic_world *w, u32 id) {
  for (u32 i = 0; i < w->n_entities; i++)
    if (w->entities[i].id == id) return &w->entities[i];
  return NULL;
}
static int world_entity_index(const mosaic_world *w, u32 id) {
  for (u32 i = 0; i < w->n_entities; i++)
    if (w->entities[i].id == id) return (int)i;
  return -1;
}
/* 随机存活玩家/实体;空 → 0 / NULL(rng 调用与状态无关,确定性保持) */
static u32 world_random_player_id(mosaic_world *w) {
  return w->n_players ? w->players[rng_below(w, w->n_players)].id : 0u;
}
static const world_entity *world_random_entity(mosaic_world *w) {
  return w->n_entities ? &w->entities[rng_below(w, w->n_entities)] : NULL;
}
/* 生成器随机方块坐标/类型(合成世界任意坐标) */
static u32 world_rng_coord(mosaic_world *w) { return rng_below(w, 1u << 20); }
static u32 world_rng_y(mosaic_world *w)     { return rng_below(w, 128) + 1; }
static u32 world_rng_block(mosaic_world *w) { return rng_below(w, 64) + 1; }
static u32 world_rng_item(mosaic_world *w)  { return rng_below(w, 1024) + 1; }

/* ---- 生命周期 ---- */
mosaic_world *mosaic_world_create(u32 seed) {
  mosaic_world *w = calloc(1, sizeof *w);
  if (!w) return NULL;
  w->seed = seed;
  w->rng = rng_seed_mix(seed);
  return w;
}
void mosaic_world_destroy(mosaic_world *w) {
  if (!w) return;
  free(w->players);
  free(w->entities);
  free(w);
}
mosaic_world_info mosaic_world_get_info(const mosaic_world *w) {
  (void)w;   /* 合成世界固定:单维度 0、出生点 (0,64,0)、无区块网格 */
  mosaic_world_info info = { WORLD_DIM_ID, 0, WORLD_SPAWN_Y, 0, 1, 0 };
  return info;
}
u32 mosaic_world_ticks(const mosaic_world *w)        { return w ? w->ticks : 0; }
u32 mosaic_world_entity_count(const mosaic_world *w) { return w ? w->n_entities : 0; }
u32 mosaic_world_player_count(const mosaic_world *w) { return w ? w->n_players : 0; }

/* ---- 动作 ----
   内部 join:分配(单调分配器,1 起,不复用 → 重复 join 同 id 不可能)+ 入表 +
   派发;返回**派发执行数**(step 计数用),*out_pid 收新 player_id
   (公开 API mosaic_world_player_join 返回 id)。 */
static u64 world_join(mosaic_world *w, mosaic_runtime *rt, u32 *out_pid) {
  u32 pid = ++w->next_player_id;
  if (w->n_players == w->cap_players) {
    u32 nc = w->cap_players ? w->cap_players * 2 : 8;
    world_player *np = realloc(w->players, (size_t)nc * sizeof *np);
    if (!np) return 0;                /* OOM:不加入,世界照常 */
    w->players = np; w->cap_players = nc;
  }
  w->players[w->n_players++].id = pid;
  w->last_join_player = pid;
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_player ev = { pid };
  memcpy(st.bytes, &ev, sizeof ev);
  u64 n = world_dispatch(rt, world_ev_id(rt, "player_join"), &st);
  if (out_pid) *out_pid = pid;
  return n;
}
u32 mosaic_world_player_join(mosaic_world *w, mosaic_runtime *rt) {
  u32 pid = 0;
  if (w) world_join(w, rt, &pid);
  return pid;
}
void mosaic_world_player_leave(mosaic_world *w, mosaic_runtime *rt, u32 player_id) {
  if (!w) return;
  int i = world_player_index(w, player_id);
  if (i < 0) return;                   /* 未知 id → no-op(文档化) */
  w->players[i] = w->players[--w->n_players];   /* swap-remove */
  if (w->last_join_player == player_id) w->last_join_player = 0;
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_player ev = { player_id };
  memcpy(st.bytes, &ev, sizeof ev);
  world_dispatch(rt, world_ev_id(rt, "player_leave"), &st);
}
void mosaic_world_player_chat(mosaic_world *w, mosaic_runtime *rt, u32 player_id) {
  if (!w || !world_player_known(w, player_id)) return;   /* 未知玩家 → no-op */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_player ev = { player_id };
  memcpy(st.bytes, &ev, sizeof ev);
  world_dispatch(rt, world_ev_id(rt, "player_chat"), &st);
}
/* 内部 spawn:分配(单调分配器,1 起)+ 出生点位置(rng 派生,确定性)+ 入表 +
   派发;返回**派发执行数**,*out_id 收新 entity_id(公开 API 返回 id)。 */
static u64 world_spawn(mosaic_world *w, mosaic_runtime *rt, u32 entity_type, u32 *out_id) {
  u32 id = ++w->next_entity_id;
  world_entity e = { id, entity_type, world_rng_coord(w), WORLD_SPAWN_Y, world_rng_coord(w) };
  if (w->n_entities == w->cap_entities) {
    u32 nc = w->cap_entities ? w->cap_entities * 2 : 8;
    world_entity *np = realloc(w->entities, (size_t)nc * sizeof *np);
    if (!np) return 0;
    w->entities = np; w->cap_entities = nc;
  }
  w->entities[w->n_entities++] = e;
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_entity ev = { e.id, e.type, e.x, e.y, e.z };
  memcpy(st.bytes, &ev, sizeof ev);
  u64 n = world_dispatch(rt, world_ev_id(rt, "entity_spawn"), &st);
  if (out_id) *out_id = id;
  return n;
}
u32 mosaic_world_entity_spawn(mosaic_world *w, mosaic_runtime *rt, u32 entity_type) {
  u32 id = 0;
  if (w) world_spawn(w, rt, entity_type, &id);
  return id;
}
void mosaic_world_entity_kill(mosaic_world *w, mosaic_runtime *rt, u32 entity_id) {
  if (!w) return;
  int i = world_entity_index(w, entity_id);
  if (i < 0) return;                   /* 未知 id → no-op(文档化) */
  world_entity e = w->entities[i];
  w->entities[i] = w->entities[--w->n_entities];        /* swap-remove */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_entity ev = { e.id, e.type, e.x, e.y, e.z };
  memcpy(st.bytes, &ev, sizeof ev);
  /* M3-2 评审 I-1:击杀派发目录事件名 entity_death(标准目录无 entity_kill;
     动作与生成器统一,消除名字接缝——否则标准目录 pack 上击杀动作静默跳过) */
  world_dispatch(rt, world_ev_id(rt, "entity_death"), &st);
}
void mosaic_world_entity_damage(mosaic_world *w, mosaic_runtime *rt, u32 entity_id, u32 amount) {
  if (!w) return;
  world_entity *e = world_entity_find(w, entity_id);
  if (!e) return;                      /* 未知实体 → no-op */
  (void)amount;  /* 载荷无伤害字段(mosaic_ev_entity 不含 amount;合成世界不维护血量),
                    参数仅作动作签名保留 */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_entity ev = { e->id, e->type, e->x, e->y, e->z };
  memcpy(st.bytes, &ev, sizeof ev);
  world_dispatch(rt, world_ev_id(rt, "entity_damage"), &st);
}
static void world_block_event(mosaic_world *w, mosaic_runtime *rt, const char *name,
                              u32 dim, u32 x, u32 y, u32 z, u32 block_type) {
  if (!w) return;
  (void)dim;  /* 载荷无维度字段(mosaic_ev_block),参数仅作签名保留 */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_block ev = { w->last_join_player, x, y, z, block_type };
  memcpy(st.bytes, &ev, sizeof ev);
  world_dispatch(rt, world_ev_id(rt, name), &st);
}
void mosaic_world_block_break(mosaic_world *w, mosaic_runtime *rt, u32 dim,
                              u32 x, u32 y, u32 z, u32 block_type) {
  /* 坐标任意、不校验存在性——合成世界不维护方块网格(注释说明) */
  world_block_event(w, rt, "block_break", dim, x, y, z, block_type);
}
void mosaic_world_block_place(mosaic_world *w, mosaic_runtime *rt, u32 dim,
                              u32 x, u32 y, u32 z, u32 block_type) {
  world_block_event(w, rt, "block_place", dim, x, y, z, block_type);
}
static void world_item_event(mosaic_world *w, mosaic_runtime *rt, const char *name,
                             u32 player_id, u32 item_id) {
  if (!w || !world_player_known(w, player_id)) return;   /* 未知玩家 → no-op */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_item ev = { player_id, item_id, 0 };   /* slot 固定 0:合成世界无背包模型 */
  memcpy(st.bytes, &ev, sizeof ev);
  world_dispatch(rt, world_ev_id(rt, name), &st);
}
void mosaic_world_item_use(mosaic_world *w, mosaic_runtime *rt, u32 player_id, u32 item_id) {
  world_item_event(w, rt, "item_use", player_id, item_id);
}
void mosaic_world_item_craft(mosaic_world *w, mosaic_runtime *rt, u32 player_id, u32 item_id) {
  world_item_event(w, rt, "item_craft", player_id, item_id);
}

/* ---- 生成器内部派发(每事件一次 dispatch,返回执行数) ---- */
static u64 gen_dispatch_entity_event(mosaic_world *w, mosaic_runtime *rt, const char *name,
                                     const world_entity *e) {
  if (!e) return 0;                      /* 无实体 → 跳过派发(骰已掷) */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_entity ev = { e->id, e->type, e->x, e->y, e->z };
  memcpy(st.bytes, &ev, sizeof ev);
  return world_dispatch(rt, world_ev_id(rt, name), &st);
}
static u64 gen_dispatch_player_event(mosaic_world *w, mosaic_runtime *rt, const char *name,
                                     u32 player_id) {
  if (!player_id) return 0;              /* 无玩家 → 跳过派发(骰已掷) */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_player ev = { player_id };
  memcpy(st.bytes, &ev, sizeof ev);
  return world_dispatch(rt, world_ev_id(rt, name), &st);
}
static u64 gen_dispatch_tick_event(mosaic_world *w, mosaic_runtime *rt, const char *name,
                                   u32 tick_no) {
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_tick ev = { tick_no };
  memcpy(st.bytes, &ev, sizeof ev);
  return world_dispatch(rt, world_ev_id(rt, name), &st);
}
static u64 gen_dispatch_block_event(mosaic_world *w, mosaic_runtime *rt, const char *name) {
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_block ev = { w->last_join_player, world_rng_coord(w), world_rng_y(w),
                         world_rng_coord(w), world_rng_block(w) };
  memcpy(st.bytes, &ev, sizeof ev);
  return world_dispatch(rt, world_ev_id(rt, name), &st);
}
static u64 gen_dispatch_item_event(mosaic_world *w, mosaic_runtime *rt, const char *name) {
  u32 pid = world_random_player_id(w);
  if (!pid) return 0;                  /* 无玩家 → 跳过派发(骰已掷) */
  world_stage st; memset(&st, 0, sizeof st);
  mosaic_ev_item ev = { pid, world_rng_item(w), 0 };
  memcpy(st.bytes, &ev, sizeof ev);
  return world_dispatch(rt, world_ev_id(rt, name), &st);
}

/* ---- 生成器速率覆盖表(唯一来源):世界可派发的全部事件入表;生成器速率模型
   以本表为唯一来源,events.h 目录 freq 是 API 语义档(供消费方参考),两者可
   独立。每行:事件名 / 生成档位(LOW、MID 入抽样轮;HIGH 每 tick 全量派发,
   不入轮,实现见 step 中 HIGH 块)/ 抽样概率 num/den / 载荷构造方式;理由列
   注明与目录档位的偏离(⚠️3 六项 + 其余 HIGH→MID/LOW 修正 + item_craft 降
   档,全部入表)。roll 序 = 表序(确定性契约的一部分,勿重排)。 ---- */
typedef enum {
  GEN_JOIN,   /* 入表 + 派发 player_join(经 world_join) */
  GEN_LEAVE,  /* 仅派发 player_leave,不动表(fire-and-forget,见头注 d)) */
  GEN_SPAWN,  /* 入表 + 派发 entity_spawn(经 world_spawn) */
  GEN_KILL,   /* 仅派发 entity_death,不动表(fire-and-forget,见头注 d)) */
  GEN_TICK,   /* 派发 tick 载荷事件(weather_change/time_change/tick) */
  GEN_BLOCK,  /* 派发方块事件(载荷 player_id = last_join_player) */
  GEN_ITEM,   /* 派发物品事件(随机玩家) */
  GEN_ENTITY, /* 派发实体事件(随机实体) */
  GEN_PLAYER, /* 派发玩家事件(随机玩家) */
} world_gen_kind;
typedef struct {
  const char *name;         /* 事件名(派发名;GEN_JOIN/GEN_SPAWN 经 world_join/spawn 派发同名) */
  mosaic_ev_freq gen_tier;  /* 生成器档位:LOW/MID 入抽样轮;HIGH 全量派发(不入轮) */
  u32 num, den;             /* 抽样概率 num/den(不入轮条目置 1/1) */
  world_gen_kind kind;      /* 载荷构造与派发方式 */
} world_freq_fix;
static const world_freq_fix g_freq_fixes[] = {
  /* ---- LOW 轮(每 tick 独立抽样;roll 序 = 表序) ---- */
  { "player_join",         MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_JOIN   }, /* 目录 LOW:进出服务器稀有 */
  { "player_leave",        MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_LEAVE  }, /* 目录 LOW */
  { "entity_spawn",        MOSAIC_EV_FREQ_LOW,  2, 1000, GEN_SPAWN  }, /* 目录 LOW;0.2% > 击杀 0.1%:实体缓慢增长 */
  { "entity_death",        MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_KILL   }, /* 目录 LOW;击杀动作/生成器统一派发目录名(I-1) */
  { "weather_change",      MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_TICK   }, /* 目录 LOW:天气变化稀有 */
  { "time_change",         MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_TICK   }, /* 目录 LOW */
  { "block_explode",       MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_BLOCK  }, /* 目录 HIGH → LOW:方块爆炸真实生态稀有(⚠️3) */
  { "entity_explode",      MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_ENTITY }, /* 目录 HIGH → LOW:实体爆炸稀有(⚠️3) */
  { "entity_combust",      MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_ENTITY }, /* 目录 HIGH → LOW:实体自燃稀有(⚠️3) */
  { "entity_fall",         MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_ENTITY }, /* 目录 HIGH → LOW:实体摔落稀有(⚠️3) */
  { "item_craft",          MOSAIC_EV_FREQ_LOW,  1, 1000, GEN_ITEM   }, /* 目录 LOW(修正:此前误按 MID 5% 升档) */
  /* ---- MID 轮(每 tick 5% 每事件独立抽样) ---- */
  { "block_break",         MOSAIC_EV_FREQ_MID,  5,  100, GEN_BLOCK  }, /* 目录 HIGH → MID:常见但非每实体每刻事件 */
  { "block_place",         MOSAIC_EV_FREQ_MID,  5,  100, GEN_BLOCK  }, /* 目录 HIGH → MID */
  { "item_use",            MOSAIC_EV_FREQ_MID,  5,  100, GEN_ITEM   }, /* 目录 HIGH → MID */
  { "entity_damage",       MOSAIC_EV_FREQ_MID,  5,  100, GEN_ENTITY }, /* 目录 HIGH → MID */
  { "player_chat",         MOSAIC_EV_FREQ_MID,  5,  100, GEN_PLAYER }, /* 目录 HIGH → MID */
  { "player_toggle_sneak", MOSAIC_EV_FREQ_MID,  5,  100, GEN_PLAYER }, /* 目录 HIGH → MID:潜行切换稀疏(⚠️3) */
  { "player_toggle_sprint",MOSAIC_EV_FREQ_MID,  5,  100, GEN_PLAYER }, /* 目录 HIGH → MID:疾跑切换稀疏(⚠️3) */
  /* ---- HIGH 档(每 tick 全量派发,不入抽样轮;实现见 step 中 HIGH 块) ---- */
  { "entity_tick",         MOSAIC_EV_FREQ_HIGH, 1,    1, GEN_ENTITY }, /* 目录 HIGH:每存活实体每 tick(位置游走) */
  { "tick",                MOSAIC_EV_FREQ_HIGH, 1,    1, GEN_TICK   }, /* 目录 HIGH:每 tick 1 次 */
};
/* 表驱动派发:按条目概率抽样一次;命中 → 按 kind 构造载荷派发(返回执行数;
   GEN_JOIN/GEN_SPAWN 返回 world_join/spawn 的派发数) */
static u64 gen_dispatch_table(mosaic_world *w, mosaic_runtime *rt, const world_freq_fix *f) {
  if (!rng_chance(w, f->num, f->den)) return 0;
  switch (f->kind) {
    case GEN_JOIN:   return world_join(w, rt, NULL);
    case GEN_LEAVE:  return gen_dispatch_player_event(w, rt, f->name, world_random_player_id(w));
    case GEN_SPAWN:  return world_spawn(w, rt, rng_below(w, 16) + 1, NULL);
    case GEN_KILL:   return gen_dispatch_entity_event(w, rt, f->name, world_random_entity(w));
    case GEN_TICK:   return gen_dispatch_tick_event(w, rt, f->name, w->ticks);
    case GEN_BLOCK:  return gen_dispatch_block_event(w, rt, f->name);
    case GEN_ITEM:   return gen_dispatch_item_event(w, rt, f->name);
    case GEN_ENTITY: return gen_dispatch_entity_event(w, rt, f->name, world_random_entity(w));
    case GEN_PLAYER: return gen_dispatch_player_event(w, rt, f->name, world_random_player_id(w));
  }
  return 0;
}

u64 mosaic_world_step(mosaic_world *w, mosaic_runtime *rt, u32 n_ticks) {
  if (!w) return 0;
  u64 total = 0;
  /* 自动补玩家(活跃度保证,不入抽样表):玩家数 0 → 强制 join 1(硬存活保证,
     不掷骰);0 < 玩家 < 3 → 掷 1/1000,命中 → join 1(补向 ≥3) */
  if (w->n_players == 0) total += world_join(w, rt, NULL);
  else if (w->n_players < 3 && rng_chance(w, 1, 1000)) total += world_join(w, rt, NULL);

  for (u32 t = 0; t < n_ticks; t++) {
    w->ticks++;
    const u32 tick_no = w->ticks;

    /* ---- LOW 档:表驱动(g_freq_fixes 为唯一来源;roll 序 = 表序,勿重排) ---- */
    for (u32 fi = 0; fi < (u32)(sizeof g_freq_fixes / sizeof g_freq_fixes[0]); fi++)
      if (g_freq_fixes[fi].gen_tier == MOSAIC_EV_FREQ_LOW)
        total += gen_dispatch_table(w, rt, &g_freq_fixes[fi]);

    /* ---- MID 档:表驱动(每 tick 5% 每事件) ---- */
    for (u32 fi = 0; fi < (u32)(sizeof g_freq_fixes / sizeof g_freq_fixes[0]); fi++)
      if (g_freq_fixes[fi].gen_tier == MOSAIC_EV_FREQ_MID)
        total += gen_dispatch_table(w, rt, &g_freq_fixes[fi]);

    /* ---- HIGH 档:每 tick 全量派发(不入抽样轮;表中标注) ----
       entity_tick 每存活实体 1 次(位置每 tick 游走 ±1)+ tick 1 次 ---- */
    for (u32 i = 0; i < w->n_entities; i++) {
      world_entity *e = &w->entities[i];
      e->x += (u32)rng_below(w, 3) - 1u;   /* -1/0/+1,u32 回绕无妨(合成世界无网格) */
      e->z += (u32)rng_below(w, 3) - 1u;
      world_stage st; memset(&st, 0, sizeof st);
      mosaic_ev_entity ev = { e->id, e->type, e->x, e->y, e->z };
      memcpy(st.bytes, &ev, sizeof ev);
      total += world_dispatch(rt, world_ev_id(rt, "entity_tick"), &st);
    }
    total += gen_dispatch_tick_event(w, rt, "tick", tick_no);
  }
  return total;
}
