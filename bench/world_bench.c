/* bench/world_bench.c — M3-3:世界场景基准。合成世界(动作 + 频率档生成器,world.c)
   驱动运行时在"真实事件流"下运行,验证核心循环(M3 目标:玩家加入→物化→执行
   →墓碑→恢复;高频 tick 下工作集 vs 函数总数):
   S-W1(门禁):单玩家生命周期循环——player_join 派发物化(处理器 ACTIVE)→ 执行
     → mosaic_evict_idle(窗口 0)墓碑 → 再 join → blob 恢复 + 执行;断言两次加入
     间 state 连续(处理器计数 1 → 2,经 blob 恢复)、ws_count 墓碑后回落、
     全循环 ≤ 500μs(S3 阈值语义)。
   S-W2(门禁):10 万函数世界 pack(订阅分布 20% player_join / 40% entity_tick /
     20% tick / 10% block_break / 10% item_use)+ 世界(E 实体)跑 n_ticks;
     断言 ws_count == 已派发事件订阅者并集(确定性种子下精确可复现)且
     ≤ 函数总数;打印工作集 vs 函数总数对比。注意:生成器 player 保活强制
     player_join 派发 → 其订阅者全部物化;分布使全部函数都是高频事件订阅者时,
     工作集 == 总数(饱和度诊断;边界以实测为准,见 gate detail 与报告)。
   S-W2b(门禁,评审补项;M3 原简报核心——Denning 局部性):同规模(10 万函数)
     但订阅稀疏——高频每 tick 事件(entity_tick/tick)订阅率降为 2%(每 50
     个函数订阅 1 个),player_join 保持 20%(step 保活强制派发),
     block_break/item_use 降为 2%(MID 1000 ticks 内必命中,若保持 10% 并集
     仍达 44%,"≪" 演示不成立),其余 72% 函数不订阅任何事件;跑 1000 ticks
     后断言 ws_count == 稀疏订阅者并集(确定性种子下精确;数值写入断言)
     且 ≪ 函数总数(密集饱和 100000 → 稀疏 ~28%,打印比值)。
   S-W3(诊断,非门禁):ticks/s、派发执行/s、平均每 tick 派发数 + 热路径派发探针。
   触发器映射注册序断言(评审必修):add_trigger 的 event_id 语义是"注册序"
   (builder finish 再重映射到排序后位置),而本文件传"排序后位置"——仅当
   WORLD_EVS 的注册序 == 长度感知排序序时两者一致(当前巧合成立)。构建期
   world_evs_reg_sorted 断言 + open 后 check_ev_registration 运行时断言
   (mosaic_runtime_event_id(rt, WORLD_EVS[i]) == i),防 WORLD_EVS 重排静默
   错位。S-W1 计时为 min-of-3(三次循环取最小,detail 打印三次值)。
   输出格式与 bench_runner 的 gate() 一致(GATE SWx PASS/FAIL);退出码 0/1。
   参数:world_bench [n_fns] [n_entities] [n_ticks] [seed](默认 100000/10/1000/
   0x1234)。 */
#include "synth_universe.h"
#include "mosaic/base.h"
#include "mosaic/runtime.h"
#include "mosaic/world.h"
#include "mosaic/event.h"
#include "mosaic/events.h"
#include "mosaic/function.h"
#include "mosaic/eviction.h"
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void gate(const char *name, int pass, const char *detail) {
  printf("GATE %-3s %s: %s\n", name, pass ? "PASS" : "FAIL", detail);
  if (!pass) g_fail = 1;
}

/* 世界可派发事件的完整目录名集(world.c g_freq_fixes 表 + 动作派发名;包注册
   全量 → 世界事件流全部流经运行时触发索引,无订阅的事件执行数为 0)。
   ⚠️ 注册序 == 长度感知排序序(见 wb_ev_cmp):add_trigger 的 event_id 语义是
   注册序(包 v2 在 finish 重映射到排序后位置),本文件把"排序后位置"当注册 id
   传入,依赖此巧合。构建期 world_evs_reg_sorted 断言 + open 后
   check_ev_registration 运行时断言(event_id(WORLD_EVS[i]) == i)双保险,
   防 WORLD_EVS 重排静默错位——重排任何条目都会立刻报错。 */
#define WORLD_EV_COUNT 20
static const char *WORLD_EVS[WORLD_EV_COUNT] = {
  "block_break", "block_explode", "block_place", "entity_combust", "entity_damage",
  "entity_death", "entity_explode", "entity_fall", "entity_spawn", "entity_tick",
  "item_craft", "item_use", "player_chat", "player_join", "player_leave",
  "player_toggle_sneak", "player_toggle_sprint", "tick", "time_change", "weather_change",
};

/* 订阅事件(每函数订阅 1 个;分布 20/40/20/10/10) */
#define SUB_EV_COUNT 5
static const char *SUB_EVS[SUB_EV_COUNT] = {
  "player_join", "entity_tick", "tick", "block_break", "item_use",
};

/* 世界 pack 构建统计(确定性,供门禁断言) */
typedef struct {
  u64 n_fns;            /* 包内函数总数 */
  u64 n_trig;           /* 触发器总数(稀疏包 56% 函数不订阅,n_trig < n_fns) */
  u32 subs[SUB_EV_COUNT];   /* 各订阅事件的订阅函数数 */
  u32 ev_id[SUB_EV_COUNT];  /* 各订阅事件在包内的 event id(排序位置) */
  u64 player_join_fn_id;    /* 第一个 player_join 订阅者 fn_id(S-W1 状态断言用) */
} world_pack_stats;

/* 与 builder finish 事件名排序/运行时二分同一长度感知比较(memcmp 前缀 + 长度
   tiebreak;名字唯一故与 strcmp 序一致) */
static int wb_ev_cmp(const void *a, const void *b) {
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  size_t lx = strlen(x), ly = strlen(y);
  size_t c = lx < ly ? lx : ly;
  int r = memcmp(x, y, c);
  if (r) return r;
  return (int)lx - (int)ly;
}

/* 注册序 == 排序序 断言:world_bench 依赖此巧合(add_trigger 的 event_id 语义
   是注册序,本文件传排序后位置;仅当 add_event 注册序 == 长度感知排序序时
   两者一致),防 WORLD_EVS 重排静默错位。构建期早失败(open 前)。 */
static int world_evs_reg_sorted(void) {
  for (u32 i = 1; i < WORLD_EV_COUNT; i++)
    if (wb_ev_cmp(&WORLD_EVS[i - 1], &WORLD_EVS[i]) >= 0) return 0;
  return 1;
}

/* 运行时注册校验(open 后):事件名 → id 必须是注册序 == 排序序
   (event_id(WORLD_EVS[i]) == i)——S-W1/S-W2/S-W2b 的 sub_ev_id 与触发器
   重映射全部依赖。防 WORLD_EVS 重排或 builder/运行时排序语义变更静默错位;
   失败时 stderr 指出首错事件并返回 0。 */
static int check_ev_registration(mosaic_runtime *rt) {
  for (u32 i = 0; i < WORLD_EV_COUNT; i++) {
    u32 id = mosaic_runtime_event_id(rt, WORLD_EVS[i]);
    if (id != i) {
      fprintf(stderr, "world_bench: event \"%s\" runtime id %u != registration"
              " index %u (registration-order == sorted-order invariant broken)\n",
              WORLD_EVS[i], id, i);
      return 0;
    }
  }
  return 1;
}

/* 订阅分布抽签(每函数至多 1 个触发器;rng 调用序即契约:每函数 1 次分布抽签,
   block_break/item_use 分支额外 1 次,勿重排——第一/二构建轮同种子同序列)。
   密集(sparse=0):player_join 20% / entity_tick 40% / tick 20% /
     block_break 10% / item_use 10%(bb/iu 共 20% 由额外 rng 平分)。全函数
     订阅 → 高频事件订阅 60%,工作集饱和到函数总数(S-W2 配套)。
   稀疏(sparse=1):高频每 tick 事件(entity_tick/tick)订阅率降为 2%(每 50
     个函数订阅 1 个),player_join 保持 20%(step 保活强制派发),bb/iu 降为
     2%(MID 5%×1000 ticks 必命中,若保持 10% 并集仍达 44%,≪ 演示不成立),
     其余 72% 不订阅(返回 -1)→ 1000 ticks 后工作集 == 并集 ≪ 函数总数
     (Denning 局部性,S-W2b 配套)。 */
static int sub_ev_for(u64 *rng, int sparse) {
  u32 r = (u32)(mosaic_bench_rng(rng) % 100);
  if (r < 20) return 0;                           /* player_join(两分布相同) */
  if (!sparse) {
    if (r < 60) return 1;                         /* entity_tick 40% */
    if (r < 80) return 2;                         /* tick 20% */
    return (mosaic_bench_rng(rng) & 1) ? 3 : 4;   /* block_break/item_use 10%+10% */
  }
  if (r < 22) return 1;                           /* entity_tick 2%(稀疏) */
  if (r < 24) return 2;                           /* tick 2%(稀疏) */
  if (r < 26) return 3;                           /* block_break 2%(稀疏) */
  if (r < 28) return 4;                           /* item_use 2%(稀疏) */
  return -1;                                      /* 72%:不订阅 */
}

/* 世界 pack 构建:n_modules 模块 × per_mod 函数(共 n_fns),事件表 = 世界可派发
   全集(WORLD_EVS),state 64B 计数(code 0 = synth_inc:每次执行 state[0]++)。
   触发 event_id = 排序后位置(与 builder/运行时同序;注册序 == 排序序依赖
   WORLD_EVS 预排序,见 world_evs_reg_sorted / check_ev_registration 断言)。
   sparse=1 时 72% 函数不订阅(无触发器),n_trig < n_fns。种子确定性:同
   (n_modules, per_mod, pack_seed, sparse) → 同订阅分配。实现为两轮同 rng
   序列(第一轮统计分布/触发总数——builder 需精确 trigger_count;第二轮实际
   构建),两轮同种子 → 同一序列 → 结果与单轮完全相同(密集数值与旧实现逐位
   一致)。返回 0 成功,-1 失败(stderr 说明)。 */
static int build_world_pack(const char *path, const char *so_path, u64 n_modules,
                            u64 per_mod, u64 pack_seed, int sparse, world_pack_stats *st) {
  char err[256];
  u64 n_fns = n_modules * per_mod;
  /* 注册序 == 排序序 构建期断言(早失败;WORLD_EVS 重排即报错,防静默错位) */
  if (!world_evs_reg_sorted()) {
    fprintf(stderr, "world pack build: WORLD_EVS not sorted by length-aware order"
            " (registration-order == sorted-order invariant broken)\n");
    return -1;
  }
  /* 订阅事件 id = 排序后位置(本地排序复制 WORLD_EVS,同比较器;因注册序 ==
     排序序,该位置同时即 add_trigger 的注册 id) */
  u32 sub_ev_id[SUB_EV_COUNT];
  const char *sorted[WORLD_EV_COUNT];
  memcpy(sorted, WORLD_EVS, sizeof sorted);
  qsort(sorted, WORLD_EV_COUNT, sizeof *sorted, wb_ev_cmp);
  for (u32 i = 0; i < SUB_EV_COUNT; i++)
    for (u32 k = 0; k < WORLD_EV_COUNT; k++)
      if (strcmp(sorted[k], SUB_EVS[i]) == 0) { sub_ev_id[i] = k; break; }

  /* 第一轮(统计):消费与构建轮完全相同的 rng 序列(cost + sub_ev_for),
     统计订阅分布 / 触发器总数 / 首个 player_join 订阅者。稀疏包 56% 函数
     无触发器,trigger_count 需在此精确计得(builder finish 要求填满)。 */
  u64 rng0 = pack_seed;
  u64 subs[SUB_EV_COUNT] = { 0 };
  u64 n_trig = 0, first_pj = 0;
  for (u64 i = 0; i < n_fns; i++) {
    (void)(mosaic_bench_rng(&rng0) % 16);              /* cost 抽签(同序) */
    int e = sub_ev_for(&rng0, sparse);
    if (e < 0) continue;
    subs[e]++;
    n_trig++;
    if (e == 0 && first_pj == 0)
      first_pj = (((i / per_mod) + 1) << 32) | (u32)(i % per_mod);
  }

  mosaic_pack_builder *b = mosaic_pack_builder_create(
      path, n_modules, n_fns, n_trig, n_modules > 1 ? n_modules - 1 : 0, WORLD_EV_COUNT);
  if (!b) return -1;
  for (u32 i = 0; i < WORLD_EV_COUNT; i++) mosaic_pack_builder_add_event(b, WORLD_EVS[i]);
  for (u64 i = 0; i < n_modules; i++) {
    char name[64], so[256];
    snprintf(name, sizeof name, "wmod_%llu", (unsigned long long)(i + 1));
    snprintf(so, sizeof so, "%s", so_path);
    mosaic_pack_builder_add_module(b, i + 1, 1, name, so);
  }
  /* 第二轮(构建):同种子重放同一 rng 序列,订阅分配与第一轮一致 */
  u64 rng = pack_seed;
  for (u64 i = 0; i < n_fns; i++) {
    u64 mi = i / per_mod;
    u32 local = (u32)(i % per_mod);
    u32 cost = (u32)(mosaic_bench_rng(&rng) % 16);
    mosaic_pack_builder_add_fn(b, mi + 1, local, 0, 64, 1, cost,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    int e = sub_ev_for(&rng, sparse);
    if (e < 0) continue;                              /* 稀疏:不订阅 */
    u64 fn_id = ((mi + 1) << 32) | local;
    mosaic_pack_builder_add_trigger(b, sub_ev_id[e], fn_id);
  }
  for (u64 i = 1; i < n_modules; i++)
    mosaic_pack_builder_add_dep(b, i + 1, i);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "world pack build: %s\n", err);
  mosaic_pack_builder_free(b);
  if (st) {
    st->n_fns = n_fns;
    st->n_trig = n_trig;
    for (u32 k = 0; k < SUB_EV_COUNT; k++) {
      st->subs[k] = (u32)subs[k];
      st->ev_id[k] = sub_ev_id[k];
    }
    st->player_join_fn_id = first_pj;
  }
  return rc;
}

/* 读处理器计数 state[0](物化/复读;失败返回 0xDEADBEEF 区分) */
static u32 handler_counter(mosaic_runtime *rt, u64 fn_id) {
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, fn_id);
  return f ? *(u32 *)f->state : 0xDEADBEEFu;
}

int main(int argc, char **argv) {
  u64 n_fns = argc > 1 ? strtoull(argv[1], NULL, 10) : 100000ull;
  u32 n_entities = argc > 2 ? (u32)strtoul(argv[2], NULL, 10) : 10u;
  u32 n_ticks = argc > 3 ? (u32)strtoul(argv[3], NULL, 10) : 1000u;
  u32 seed = argc > 4 ? (u32)strtoul(argv[4], NULL, 16) : 0x1234u;
  if (n_ticks < 1) n_ticks = 1;
  const char *so = "build/bench/synth_mod.so";
  char err[256];
  mosaic_evict_config zcfg = { 0 };   /* 窗口 0:驱逐一切无租约且空闲的 ACTIVE 函数 */
  char detail[256];

  printf("world_bench: n_fns=%llu n_entities=%u n_ticks=%u seed=0x%x\n",
         (unsigned long long)n_fns, n_entities, n_ticks, seed);

  /* ---- S-W1:单玩家生命周期(门禁:state 连续 + ws 回落 + 全循环 ≤ 500μs) ---- */
  world_pack_stats stw;
  if (build_world_pack("bench/world_small.pack", so, 1, 10, 0x5EEDull, 0, &stw) != 0) {
    gate("SW1", 0, "world pack build failed"); return 1;
  }
  mosaic_runtime *r1 = mosaic_runtime_open("bench/world_small.pack", err, sizeof err);
  if (!r1) { gate("SW1", 0, err); return 1; }
  if (!check_ev_registration(r1)) {
    gate("SW1", 0, "event id != registration index (see stderr)"); return 1;
  }
  if (stw.subs[0] == 0 || stw.player_join_fn_id == 0) {
    gate("SW1", 0, "no player_join subscriber in small pack"); return 1;
  }
  /* 断言阶段(玩家 join → 物化 ACTIVE → 执行 → evict 墓碑 → blob 恢复 → 再 join) */
  mosaic_world *w1 = mosaic_world_create(0xBEEF);
  u32 p1 = mosaic_world_player_join(w1, r1);              /* 派发物化 + 执行(计数 1) */
  u32 ws_active = mosaic_runtime_working_set_count(r1);
  u32 c1 = handler_counter(r1, stw.player_join_fn_id);
  mosaic_evict_idle(r1, &zcfg);                            /* 窗口 0 → 墓碑 */
  u32 ws_tomb = mosaic_runtime_working_set_count(r1);
  u32 c_restored = handler_counter(r1, stw.player_join_fn_id);  /* 经 blob 恢复(计数 1) */
  u32 ws_restored = mosaic_runtime_working_set_count(r1);
  mosaic_evict_idle(r1, &zcfg);                            /* 清场(计时段从空 ws 起) */
  u32 p2 = mosaic_world_player_join(w1, r1);              /* 再 join → 恢复 + 执行(计数 2) */
  u32 c2 = handler_counter(r1, stw.player_join_fn_id);
  int sw1_state = (c1 == 1 && c_restored == 1 && c2 == 2 && p2 > p1);
  int sw1_ws = (ws_active == stw.subs[0] && ws_tomb == 0 && ws_restored == stw.subs[0]);
  mosaic_world_destroy(w1);

  /* 计时阶段:全循环(复用 S3 阈值语义)= join → evict(窗口 0) → join;
     min-of-3:三次计时取最小(冷缓存/偶发抖动不入门禁),detail 打印三次值 */
  mosaic_world *w1b = mosaic_world_create(0xBEEF ^ 1u);
  double t_runs[3];
  for (u32 i = 0; i < 3; i++) {
    double t0 = mosaic_bench_now_us();
    mosaic_world_player_join(w1b, r1);
    mosaic_evict_idle(r1, &zcfg);
    mosaic_world_player_join(w1b, r1);
    t_runs[i] = mosaic_bench_now_us() - t0;
  }
  mosaic_world_destroy(w1b);
  double t_cycle_us = t_runs[0];
  for (u32 i = 1; i < 3; i++)
    if (t_runs[i] < t_cycle_us) t_cycle_us = t_runs[i];
  snprintf(detail, sizeof detail,
           "state 1->2 continuous (c1=%u restored=%u c2=%u, p1=%u p2=%u),"
           " ws %u->0->%u (subs %u), cycle min %.1f us (runs %.1f/%.1f/%.1f),"
           " limit %.0f us",
           c1, c_restored, c2, p1, p2, ws_active, ws_restored, stw.subs[0],
           t_cycle_us, t_runs[0], t_runs[1], t_runs[2], 500.0);
  gate("SW1", sw1_state && sw1_ws && t_cycle_us <= 500.0, detail);
  mosaic_runtime_close(r1);

  /* ---- S-W2:工作集 vs 函数总数(门禁) ----
     世界 pack(默认 50 模块 × 2000 函数 = 10 万)+ 世界(E 实体)跑 n_ticks。
     工作集 = 已派发事件订阅者并集:1000 ticks 内 5 个订阅事件全部派发
     (step 起始 keepalive 强制 player_join;entity_tick/tick 每 tick;block_break/
     item_use MID 5%×1000 → 各 ~50 次)→ 并集 == n_fns。分布使全部函数都是
     高频事件订阅者,工作集饱和到 == 总数(门禁边界以实测并集为准;稀疏分布时
     工作集 ≪ 总数——Denning 局部性,见报告)。 ---- */
  world_pack_stats stw2;
  u64 n_modules = n_fns / 2000;
  if (n_modules < 1) n_modules = 1;
  u64 per_mod = n_fns / n_modules;
  if (build_world_pack("bench/world.pack", so, n_modules, per_mod, 0x1D0Aull, 0, &stw2) != 0) {
    gate("SW2", 0, "world pack build failed"); return 1;
  }
  mosaic_runtime *r2 = mosaic_runtime_open("bench/world.pack", err, sizeof err);
  if (!r2) { gate("SW2", 0, err); return 1; }
  if (!check_ev_registration(r2)) {
    gate("SW2", 0, "event id != registration index (see stderr)"); return 1;
  }
  /* 世界:预生成 E 实体(动作派发 entity_spawn,无订阅者,不物化;id/类型确定性) */
  mosaic_world *w2 = mosaic_world_create(seed);
  for (u32 i = 0; i < n_entities; i++) mosaic_world_entity_spawn(w2, r2, (i % 16) + 1);
  u64 executed_total = 0;
  double tw0 = mosaic_bench_now_us();
  executed_total += mosaic_world_step(w2, r2, 1);          /* 第 1 tick 后工作集(诊断:饱和) */
  u32 ws_tick1 = mosaic_runtime_working_set_count(r2);
  executed_total += mosaic_world_step(w2, r2, n_ticks - 1);
  u32 ws_final = mosaic_runtime_working_set_count(r2);
  double t_world_s = (mosaic_bench_now_us() - tw0) / 1e6;
  u64 fn_total = mosaic_runtime_function_count(r2);
  /* 已派发事件订阅者并集 = 全函数(5 个订阅事件 1000 ticks 内全部派发;确定性) */
  u64 expected_ws = stw2.n_fns;
  snprintf(detail, sizeof detail,
           "ws_count %u vs total %llu (tick1 %u, %s), subs pj/et/t/bb/iu ="
           " %u/%u/%u/%u/%u, world %.2fs, expected(subscriber union) %llu,"
           " limit %llu",
           ws_final, (unsigned long long)fn_total, ws_tick1,
           t_world_s > 0 ? "saturated" : "n/a",
           stw2.subs[0], stw2.subs[1], stw2.subs[2], stw2.subs[3], stw2.subs[4],
           t_world_s, (unsigned long long)expected_ws, (unsigned long long)expected_ws);
  gate("SW2", ws_final == expected_ws && ws_final <= fn_total, detail);

  /* ---- S-W3:吞吐(诊断,非门禁)----
     数值取自 S-W2 的同一世界运行(同 seed → 同事件流;不重复跑 1000 ticks)。
     ticks/s、派发执行/s(世界 API 的"派发"= 成功执行的订阅调用数)、
     平均每 tick 派发数;另加热路径派发探针(直发 entity_tick)。 */
  printf("S-W3 DIAG: world %.2fs, %.0f ticks/s, %.0f dispatched-executions/s,"
         " %.1f per tick (%u entities, %u ticks)\n",
         t_world_s, t_world_s > 0 ? (double)n_ticks / t_world_s : 0.0,
         t_world_s > 0 ? (double)executed_total / t_world_s : 0.0,
         n_ticks ? (double)executed_total / n_ticks : 0.0,
         n_entities, n_ticks);
  /* 热路径派发探针:直发 entity_tick(工作集已饱和,全命中订阅者) */
  u32 ev_et = mosaic_runtime_event_id(r2, "entity_tick");
  if (ev_et != MOSAIC_U32_NONE) {
    mosaic_ev_entity payload = { 1, 1, 0, 64, 0 };
    const u32 NPROBE = 100;
    double tp0 = mosaic_bench_now_us();
    u32 acc = 0;
    for (u32 i = 0; i < NPROBE; i++) acc += mosaic_event_dispatch(r2, ev_et, &payload);
    double tp_s = (mosaic_bench_now_us() - tp0) / 1e6;
    printf("S-W3 DIAG: hot dispatch probe entity_tick x%u (all %u subs resident)"
           " -> %.0f dispatches/s, %u executed/dispatch, %.1f us/dispatch\n",
           NPROBE, stw2.subs[1], tp_s > 0 ? (double)NPROBE / tp_s : 0.0,
           NPROBE ? acc / NPROBE : 0u, tp_s > 0 ? tp_s * 1e6 / NPROBE : 0.0);
  }
  mosaic_world_destroy(w2);
  mosaic_runtime_close(r2);

  /* ---- S-W2b:稀疏订阅工作集(门禁;M3 原简报核心——Denning 局部性) ----
     与 S-W2 同规模(10 万函数)但订阅稀疏:高频每 tick 事件(entity_tick/tick)
     订阅率 40%+20% → 2%+2%(每 50 个函数订阅 1 个),player_join 保持 20%
     (step 保活强制派发),block_break/item_use 降为 2%(MID 5%×1000 ticks
     必命中;若保持 10% 并集仍达 44%,≪ 演示不成立),其余 72% 函数不订阅。
     1000 ticks 内 5 个订阅事件全部派发(player_join 保活强制;entity_tick/tick
     每 tick;bb/iu ~50 次命中)→ 工作集 == 订阅者并集(确定性种子下精确;
     数值写入断言,机制如注释)且 ≪ 函数总数:密集饱和 100000 → 稀疏 ~28%,
     打印比值——"高频事件订阅稀疏 ⇒ 工作集 ≪ 总数"即 M3 原简报核心。
     tick1 = pj+et+tick 并集(bb/iu 未命中;世界同 seed → 事件流与 S-W2 一致)。 */
  world_pack_stats stw2b;
  if (build_world_pack("bench/world_sparse.pack", so, n_modules, per_mod,
                       0x2B0Aull, 1, &stw2b) != 0) {
    gate("SW2b", 0, "sparse world pack build failed"); return 1;
  }
  mosaic_runtime *r2b = mosaic_runtime_open("bench/world_sparse.pack", err, sizeof err);
  if (!r2b) { gate("SW2b", 0, err); return 1; }
  if (!check_ev_registration(r2b)) {
    gate("SW2b", 0, "event id != registration index (see stderr)"); return 1;
  }
  mosaic_world *w2b = mosaic_world_create(seed);
  for (u32 i = 0; i < n_entities; i++) mosaic_world_entity_spawn(w2b, r2b, (i % 16) + 1);
  mosaic_world_step(w2b, r2b, 1);
  u32 ws2b_tick1 = mosaic_runtime_working_set_count(r2b);
  mosaic_world_step(w2b, r2b, n_ticks - 1);
  u32 ws2b = mosaic_runtime_working_set_count(r2b);
  u64 fn_total2b = mosaic_runtime_function_count(r2b);
  /* 并集 == 5 个订阅事件订阅者之和(全部派发;稀疏包其他事件无订阅者)。
     稀疏分布 pj 20% + et/tick 2%+2% + bb/iu 2%+2% ≈ 28% 总数(实测写入,
     如 pj/et/t/bb/iu = x/x/x/x/x);≪ 断言为严格 < 1/2(密集饱和 100% 的
     对照;分布漂移超阈即 FAIL)。 */
  u64 exp2b = (u64)stw2b.subs[0] + stw2b.subs[1] + stw2b.subs[2] +
              stw2b.subs[3] + stw2b.subs[4];
  double ratio2b = fn_total2b ? (double)ws2b / (double)fn_total2b : 0.0;
  int sw2b_ok = (u64)ws2b == exp2b && (u64)ws2b <= fn_total2b &&
                (u64)ws2b * 2 < fn_total2b;
  snprintf(detail, sizeof detail,
           "ws=%u/%llu (sparse: 2%% entity subscribers), ratio %.3f, tick1 %u,"
           " subs pj/et/t/bb/iu = %u/%u/%u/%u/%u (n_trig %llu),"
           " expected(union) %llu, limit(1/2 total) %llu",
           ws2b, (unsigned long long)fn_total2b, ratio2b, ws2b_tick1,
           stw2b.subs[0], stw2b.subs[1], stw2b.subs[2], stw2b.subs[3],
           stw2b.subs[4], (unsigned long long)stw2b.n_trig,
           (unsigned long long)exp2b, (unsigned long long)(fn_total2b / 2));
  gate("SW2b", sw2b_ok, detail);
  mosaic_world_destroy(w2b);
  mosaic_runtime_close(r2b);

  printf(g_fail ? "\nGATES FAILED\n" : "\nALL GATES PASSED\n");
  return g_fail ? 1 : 0;
}
