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

/* ---- 频率档生成器(每 tick 序列,档位见 events.h freq 语义) ----
   a) HIGH 档:entity_tick 对每个存活实体派发 1 次(载荷 {entity_id, type,
      位置;位置每 tick 随机游走 ±1,确定性})+ tick 派发 1 次(载荷 {tick_no})
   b) MID 档:block_break/block_place/item_use/item_craft/entity_damage/
      player_chat 每 tick 5% 独立抽样(种子 rng,确定性)
   c) LOW 档:player_join/player_leave/entity_spawn/entity_kill/
      weather_change/time_change 每 tick 0.1% 独立抽样(entity_spawn 0.2%,
      略高于击杀 0.1% —— 实体数随 step 缓慢增长);world_step 开始时若玩家数
      < 3 且概率命中 → 自动补玩家(玩家数 0 时强制 join 1 个——硬存活保证,
      保持世界活跃)
   ⚠️3 承载(M3-1 评审):block_explode/entity_explode/entity_combust/
   entity_fall/player_toggle_sneak/player_toggle_sprint 在目录中标 HIGH 但
   真实生态为稀有(爆炸/自燃/摔落/潜行切换/疾跑切换均低频);目录 API 保持
   (events.h 不改),生成器消费侧以覆盖表 g_freq_fixes 修正为 MID/LOW 抽样。
   每个事件都掷骰后按需派发(rng 调用序与状态无关,保证确定性)。 */

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
  world_dispatch(rt, world_ev_id(rt, "entity_kill"), &st);
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

/* ---- ⚠️3 覆盖表:目录标 HIGH 但真实生态稀有的事件 → 生成器消费侧降档。
   events.h 目录 API 保持(不改标注);本表为生成器的抽样档位修正(消费侧),
   step 按表驱动 LOW/MID 两轮抽样(roll 序 = 表序,确定性契约的一部分)。 ---- */
typedef enum { FIX_BLOCK, FIX_ENTITY, FIX_PLAYER } world_fix_kind;
typedef struct { const char *name; mosaic_ev_freq gen_tier; world_fix_kind kind; } world_freq_fix;
static const world_freq_fix g_freq_fixes[] = {
  { "block_explode",        MOSAIC_EV_FREQ_LOW, FIX_BLOCK },   /* 方块爆炸:真实生态稀有 */
  { "entity_explode",       MOSAIC_EV_FREQ_LOW, FIX_ENTITY },  /* 实体爆炸 */
  { "entity_combust",       MOSAIC_EV_FREQ_LOW, FIX_ENTITY },  /* 实体自燃 */
  { "entity_fall",          MOSAIC_EV_FREQ_LOW, FIX_ENTITY },  /* 实体摔落 */
  { "player_toggle_sneak",  MOSAIC_EV_FREQ_MID, FIX_PLAYER },  /* 潜行切换:比 HIGH 档稀疏 */
  { "player_toggle_sprint", MOSAIC_EV_FREQ_MID, FIX_PLAYER },  /* 疾跑切换 */
};
/* 表驱动派发:按修正档抽样一次;命中 → 按 kind 构造载荷派发 */
static u64 gen_dispatch_fix(mosaic_world *w, mosaic_runtime *rt, const world_freq_fix *f,
                            u32 num, u32 den) {
  if (!rng_chance(w, num, den)) return 0;
  switch (f->kind) {
    case FIX_BLOCK:  return gen_dispatch_block_event(w, rt, f->name);
    case FIX_ENTITY: return gen_dispatch_entity_event(w, rt, f->name, world_random_entity(w));
    case FIX_PLAYER: return gen_dispatch_player_event(w, rt, f->name, world_random_player_id(w));
  }
  return 0;
}

u64 mosaic_world_step(mosaic_world *w, mosaic_runtime *rt, u32 n_ticks) {
  if (!w) return 0;
  u64 total = 0;
  /* 自动补玩家(保持世界活跃):玩家数 0 → 强制 join 1(硬存活保证,不掷骰);
     0 < 玩家 < 3 → 掷 LOW 骰,命中 → join 1(补向 ≥3) */
  if (w->n_players == 0) total += world_join(w, rt, NULL);
  else if (w->n_players < 3 && rng_chance(w, 1, 1000)) total += world_join(w, rt, NULL);

  for (u32 t = 0; t < n_ticks; t++) {
    w->ticks++;
    const u32 tick_no = w->ticks;

    /* ---- LOW 档(每 tick,每事件独立抽样;rng 调用序固定,勿重排) ---- */
    if (rng_chance(w, 1, 1000)) total += world_join(w, rt, NULL);
    if (rng_chance(w, 1, 1000))
      total += gen_dispatch_player_event(w, rt, "player_leave", world_random_player_id(w));
    if (rng_chance(w, 2, 1000))   /* entity_spawn 0.2% > entity_kill 0.1%:实体缓慢增长 */
      total += world_spawn(w, rt, rng_below(w, 16) + 1, NULL);
    if (rng_chance(w, 1, 1000))
      total += gen_dispatch_entity_event(w, rt, "entity_kill", world_random_entity(w));
    if (rng_chance(w, 1, 1000)) total += gen_dispatch_tick_event(w, rt, "weather_change", tick_no);
    if (rng_chance(w, 1, 1000)) total += gen_dispatch_tick_event(w, rt, "time_change", tick_no);
    /* ⚠️3 修正:目录 HIGH → 生成器按 LOW(覆盖表驱动,见 g_freq_fixes) */
    for (u32 fi = 0; fi < (u32)(sizeof g_freq_fixes / sizeof g_freq_fixes[0]); fi++)
      if (g_freq_fixes[fi].gen_tier == MOSAIC_EV_FREQ_LOW)
        total += gen_dispatch_fix(w, rt, &g_freq_fixes[fi], 1, 1000);

    /* ---- MID 档(每 tick 5% 每事件) ---- */
    if (rng_chance(w, 5, 100)) total += gen_dispatch_block_event(w, rt, "block_break");
    if (rng_chance(w, 5, 100)) total += gen_dispatch_block_event(w, rt, "block_place");
    if (rng_chance(w, 5, 100)) total += gen_dispatch_item_event(w, rt, "item_use");
    if (rng_chance(w, 5, 100)) total += gen_dispatch_item_event(w, rt, "item_craft");
    if (rng_chance(w, 5, 100))
      total += gen_dispatch_entity_event(w, rt, "entity_damage", world_random_entity(w));
    if (rng_chance(w, 5, 100))
      total += gen_dispatch_player_event(w, rt, "player_chat", world_random_player_id(w));
    /* ⚠️3 修正:目录 HIGH → 生成器按 MID(覆盖表驱动,见 g_freq_fixes) */
    for (u32 fi = 0; fi < (u32)(sizeof g_freq_fixes / sizeof g_freq_fixes[0]); fi++)
      if (g_freq_fixes[fi].gen_tier == MOSAIC_EV_FREQ_MID)
        total += gen_dispatch_fix(w, rt, &g_freq_fixes[fi], 5, 100);

    /* ---- HIGH 档:entity_tick 每存活实体 1 次(位置每 tick 游走 ±1)+ tick 1 次 ---- */
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
