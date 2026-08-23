/* tests/test_world.c — M3-2:合成世界模拟器。
   世界基础(create/destroy/计数/ticks)、动作 → 事件派发(每事件 1 订阅函数,
   state 计数)、事件名未注册跳过派发、标准目录 pack 全链路(M3-2 评审 I-1:
   事件名取自 events.h,mosaic_event_spec_by_name 接缝守卫)、生成器(同 seed
   确定性 / HIGH 档 entity_tick 计数 = 实体数 × ticks / 世界活跃与实体增长)、
   载荷正确性(code_off 5 载荷探针把事件载荷拷贝进 state 供断言 == 动作参数)。
   需要 test_mod.so fixture(CMake 传入;code_off 5 = code_payload_probe)。 */
#include "mosaic/base.h"
#include "mosaic/events.h"
#include "mosaic/event.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic/world.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SO_PATH;

#define MOD_ID 10ull
static u64 wfn(u32 local) { return (MOD_ID << 32) | local; }

/* 长度感知比较(与 builder 事件名排序/运行时二分同一语义) */
static int ev_cmp(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t c = la < lb ? la : lb;
  int r = memcmp(a, b, c);
  if (r) return r;
  return (int)la - (int)lb;
}

/* ---- 事件集 A:14 个事件(注册序 == 排序序 → trigger id = 注册下标;
     运行时 event_id 断言 == 下标,验证排序无偏;击杀统一派发目录事件名
     entity_death —— M3-2 评审 I-1) ---- */
static const char *EVS_A[14] = {
  "block_break", "block_place", "entity_damage", "entity_death", "entity_spawn",
  "entity_tick", "item_craft", "item_use", "player_chat", "player_join",
  "player_leave", "tick", "time_change", "weather_change",
};

#define FN_FLAGS (MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE)

/* 每事件 1 个 code_inc 订阅函数(local_id = 事件下标) */
static int build_pack_a(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_world_a.pack", 1, 14, 14, 0, 14);
  if (!b) return -1;
  for (u32 i = 0; i < 14; i++) mosaic_pack_builder_add_event(b, EVS_A[i]);
  mosaic_pack_builder_add_module(b, MOD_ID, 1, "mod_a", SO_PATH);
  for (u32 i = 0; i < 14; i++)
    mosaic_pack_builder_add_fn(b, MOD_ID, i, 0, 64, 1, 0, FN_FLAGS);
  for (u32 i = 0; i < 14; i++) mosaic_pack_builder_add_trigger(b, i, wfn(i));
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}
static mosaic_runtime *open_pack_a(void) {
  char err[256];
  if (build_pack_a()) return NULL;
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_world_a.pack", err, sizeof err);
  if (!rt) fprintf(stderr, "open: %s\n", err);
  return rt;
}
static u32 ev_counter(mosaic_runtime *rt, u32 local) {
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, wfn(local));
  return f ? *(u32 *)f->state : 0xDEADBEEFu;
}
static u32 ev_last_event(mosaic_runtime *rt, u32 local) {
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, wfn(local));
  return f ? ((u32 *)f->state)[1] : 0xDEADBEEFu;
}

/* ---- 世界基础:create/destroy、计数、ticks 推进、NULL rt 安全 ---- */
static void test_world_basics(void) {
  mosaic_world *w = mosaic_world_create(0xC0FFEE);
  MT_CHECK(w != NULL);
  if (!w) return;
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 0);
  MT_CHECK_EQ_U64(mosaic_world_entity_count(w), 0);
  MT_CHECK_EQ_U64(mosaic_world_player_count(w), 0);
  mosaic_world_info info = mosaic_world_get_info(w);
  MT_CHECK_EQ_U64(info.dimension_id, 0);
  MT_CHECK_EQ_U64(info.dimension_count, 1);
  MT_CHECK_EQ_U64(info.chunk_count, 0);
  u32 p1 = mosaic_world_player_join(w, NULL);   /* NULL rt:事件解析 NONE → 跳过派发 */
  u32 p2 = mosaic_world_player_join(w, NULL);
  MT_CHECK_EQ_U64(p1, 1);                       /* 单调分配器:1 起,不复用 */
  MT_CHECK_EQ_U64(p2, 2);
  MT_CHECK_EQ_U64(mosaic_world_player_count(w), 2);
  u32 e1 = mosaic_world_entity_spawn(w, NULL, 7);
  MT_CHECK_EQ_U64(e1, 1);
  MT_CHECK_EQ_U64(mosaic_world_entity_count(w), 1);
  u64 s = mosaic_world_step(w, NULL, 10);
  MT_CHECK_EQ_U64(s, 0);                        /* 无派发(无 rt),世界照常推进 */
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 10);
  mosaic_world_destroy(w);
}

/* ---- 动作 → 事件:join×2 → counter 2;leave/chat/spawn/kill/damage/
     break/place/use/craft → counter 1;未知 id → no-op 无派发 ---- */
static void test_actions_dispatch_events(void) {
  mosaic_runtime *rt = open_pack_a();
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (u32 i = 0; i < 14; i++)                  /* 运行时 id == 排序下标 */
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, EVS_A[i]), i);

  mosaic_world *w = mosaic_world_create(0xABCD);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }

  u32 p1 = mosaic_world_player_join(w, rt);     /* join 2 次 → counter 2 */
  u32 p2 = mosaic_world_player_join(w, rt);
  MT_CHECK_EQ_U64(p1, 1); MT_CHECK_EQ_U64(p2, 2);
  MT_CHECK_EQ_U64(ev_counter(rt, 9), 2);        /* player_join */

  mosaic_world_player_leave(w, rt, p1);
  MT_CHECK_EQ_U64(ev_counter(rt, 10), 1);       /* player_leave */
  mosaic_world_player_chat(w, rt, p2);
  MT_CHECK_EQ_U64(ev_counter(rt, 8), 1);        /* player_chat */

  u32 e1 = mosaic_world_entity_spawn(w, rt, 5);
  MT_CHECK_EQ_U64(e1, 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 4), 1);        /* entity_spawn */
  mosaic_world_entity_damage(w, rt, e1, 3);
  MT_CHECK_EQ_U64(ev_counter(rt, 2), 1);        /* entity_damage */
  mosaic_world_entity_kill(w, rt, e1);
  MT_CHECK_EQ_U64(ev_counter(rt, 3), 1);        /* entity_death */

  mosaic_world_block_break(w, rt, 0, 1, 2, 3, 7);
  MT_CHECK_EQ_U64(ev_counter(rt, 0), 1);        /* block_break */
  mosaic_world_block_place(w, rt, 0, 4, 5, 6, 8);
  MT_CHECK_EQ_U64(ev_counter(rt, 1), 1);        /* block_place */
  mosaic_world_item_use(w, rt, p2, 100);
  MT_CHECK_EQ_U64(ev_counter(rt, 7), 1);        /* item_use */
  mosaic_world_item_craft(w, rt, p2, 200);
  MT_CHECK_EQ_U64(ev_counter(rt, 6), 1);        /* item_craft */

  /* 未知/重复 id → no-op 无派发(文档化语义) */
  mosaic_world_player_leave(w, rt, 999);
  mosaic_world_player_leave(w, rt, p1);         /* 重复 leave(已移除) */
  mosaic_world_player_chat(w, rt, 999);
  mosaic_world_entity_kill(w, rt, 999);
  mosaic_world_entity_damage(w, rt, 999, 1);
  mosaic_world_item_use(w, rt, 999, 1);
  mosaic_world_item_craft(w, rt, 999, 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 10), 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 8), 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 3), 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 2), 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 7), 1);
  MT_CHECK_EQ_U64(ev_counter(rt, 6), 1);

  /* last_event 抽查:state[1] == 运行时事件 id */
  MT_CHECK_EQ_U64(ev_last_event(rt, 0), mosaic_runtime_event_id(rt, "block_break"));
  MT_CHECK_EQ_U64(ev_last_event(rt, 9), mosaic_runtime_event_id(rt, "player_join"));

  MT_CHECK_EQ_U64(mosaic_world_player_count(w), 1);   /* p2 仍活跃 */
  MT_CHECK_EQ_U64(mosaic_world_entity_count(w), 0);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

/* ---- 载荷正确性:code_off 5 探针把载荷拷进 state;断言 == 动作参数;
     暂存区载荷之外字节必须为零(零化契约) ---- */
static int build_pack_probe(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_world_b.pack", 1, 1, 1, 0, 1);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, MOD_ID, 1, "mod_b", SO_PATH);
  mosaic_pack_builder_add_fn(b, MOD_ID, 0, 5, 64, 1, 0, FN_FLAGS);  /* code_off 5:载荷探针 */
  mosaic_pack_builder_add_trigger(b, 0, wfn(0));
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}
static void test_payload_probe(void) {
  char err[256];
  MT_CHECK(build_pack_probe() == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_world_b.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "block_break"), 0);
  mosaic_world *w = mosaic_world_create(0x50);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }
  u32 pid = mosaic_world_player_join(w, rt);
  mosaic_world_block_break(w, rt, 0, 10, 20, 30, 7);
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, wfn(0));
  MT_CHECK(f != NULL);
  if (!f) { mosaic_world_destroy(w); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)f->state, 1);                     /* counter:一次动作 */
  MT_CHECK_EQ_U64(((u32 *)f->state)[1], 0);                 /* last_event:block_break id */
  mosaic_ev_block ev;
  memcpy(&ev, (const u8 *)f->state + 8, sizeof ev);
  MT_CHECK_EQ_U64(ev.player_id, pid);                       /* 最近加入的玩家 */
  MT_CHECK_EQ_U64(ev.x, 10); MT_CHECK_EQ_U64(ev.y, 20);
  MT_CHECK_EQ_U64(ev.z, 30); MT_CHECK_EQ_U64(ev.block_type, 7);
  /* 暂存缓冲零化契约:载荷(20B)之后的 56B 暂存区字节必须为零 */
  const u8 *payload = (const u8 *)f->state + 8;
  for (int i = 20; i < 56; i++) MT_CHECK_EQ_U64(payload[i], 0);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

/* ---- 事件名未注册:无事件 pack 上全部动作/step 不崩、派发计数 0、
     世界状态照常推进;事件已注册但无订阅:派发执行 0 ---- */
static int build_pack_noevents(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_world_c.pack", 1, 1, 0, 0, 0);
  if (!b) return -1;
  mosaic_pack_builder_add_module(b, MOD_ID, 1, "mod_c", SO_PATH);
  mosaic_pack_builder_add_fn(b, MOD_ID, 0, 0, 64, 1, 0, FN_FLAGS);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}
static int build_pack_events_no_subscribers(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_world_d.pack", 1, 1, 0, 0, 2);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_module(b, MOD_ID, 1, "mod_d", SO_PATH);
  mosaic_pack_builder_add_fn(b, MOD_ID, 0, 0, 64, 1, 0, FN_FLAGS);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}
static void test_unregistered_events_noop(void) {
  char err[256];
  /* 无事件 pack:全部事件名 → NONE → 跳过派发;世界状态照常推进 */
  MT_CHECK(build_pack_noevents() == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_world_c.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_join"), MOSAIC_U32_NONE);
  mosaic_world *w = mosaic_world_create(7);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }
  u32 p = mosaic_world_player_join(w, rt);
  MT_CHECK_EQ_U64(p, 1);
  mosaic_world_player_leave(w, rt, p);
  mosaic_world_player_chat(w, rt, p);
  mosaic_world_entity_spawn(w, rt, 3);
  mosaic_world_entity_kill(w, rt, 999);       /* 未知 id → no-op */
  mosaic_world_entity_damage(w, rt, 999, 3);  /* 未知 id → no-op */
  mosaic_world_block_break(w, rt, 0, 1, 2, 3, 7);
  mosaic_world_block_place(w, rt, 0, 1, 2, 3, 8);
  mosaic_world_item_use(w, rt, p, 10);
  mosaic_world_item_craft(w, rt, p, 20);
  MT_CHECK_EQ_U64(mosaic_world_player_count(w), 0);    /* 世界状态仍变 */
  MT_CHECK_EQ_U64(mosaic_world_entity_count(w), 1);
  u64 s = mosaic_world_step(w, rt, 10);
  MT_CHECK_EQ_U64(s, 0);                                /* 事件未注册:派发计数 0 */
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 10);           /* 世界仍推进 */
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);

  /* 事件已注册但无订阅:event_id 命中 → 派发执行 0,世界照常推进 */
  MT_CHECK(build_pack_events_no_subscribers() == 0);
  rt = mosaic_runtime_open("/tmp/mosaic_test_world_d.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK(mosaic_runtime_event_id(rt, "block_break") != MOSAIC_U32_NONE);
  w = mosaic_world_create(8);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(mosaic_world_step(w, rt, 5), 0);      /* 无订阅:执行 0 */
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 5);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

/* ---- M3-2 评审 I-1:标准目录 pack 全链路。事件名全部取自 events.h 目录
     (mosaic_event_spec_by_name 逐个命中——名字接缝守卫:世界全部派发名必须
     在标准目录内,否则动作/生成器在标准目录 pack 上静默跳过);动作全链路
     join/spawn/kill/break 等派发后订阅计数正确;生成器在标准目录上按模型
     跑通(LOW/MID 修正档事件在长窗口内命中、计数增长)。 ---- */
static void test_standard_catalog_chain(void) {
  /* 世界派发名全集:动作 10 + 生成器 20 的并集(按目录长度感知序排 =
     runtime event_id == 排序下标 的断言前提) */
  static const char *CAT[20] = {
    "block_break", "block_explode", "block_place",
    "entity_combust", "entity_damage", "entity_death", "entity_explode",
    "entity_fall", "entity_spawn", "entity_tick",
    "item_craft", "item_use",
    "player_chat", "player_join", "player_leave",
    "player_toggle_sneak", "player_toggle_sprint",
    "tick", "time_change", "weather_change",
  };
  /* 名字接缝守卫:每个派发名都必须在标准目录内 */
  for (u32 i = 0; i < 20; i++)
    MT_CHECK(mosaic_event_spec_by_name(CAT[i]) != NULL);

  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(
      "/tmp/mosaic_test_world_cat.pack", 1, 20, 20, 0, 20);
  if (!b) return;
  for (u32 i = 0; i < 20; i++) mosaic_pack_builder_add_event(b, CAT[i]);
  mosaic_pack_builder_add_module(b, MOD_ID, 1, "mod_cat", SO_PATH);
  for (u32 i = 0; i < 20; i++)
    mosaic_pack_builder_add_fn(b, MOD_ID, i, 0, 64, 1, 0, FN_FLAGS);
  for (u32 i = 0; i < 20; i++) mosaic_pack_builder_add_trigger(b, i, wfn(i));
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  if (rc) return;
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_world_cat.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (u32 i = 0; i < 20; i++)
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, CAT[i]), i);   /* 全部注册,id == 目录排序位 */

  mosaic_world *w = mosaic_world_create(0x1234u);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }

  /* 动作全链路:join/spawn/kill/break/place/use/craft/chat/damage/leave,
     每事件订阅计数精确 —— 标准目录下无名字接缝 */
  u32 p1 = mosaic_world_player_join(w, rt);
  u32 p2 = mosaic_world_player_join(w, rt);
  u32 p3 = mosaic_world_player_join(w, rt);
  MT_CHECK_EQ_U64(ev_counter(rt, 13), 3);              /* player_join */
  mosaic_world_player_leave(w, rt, p1);
  MT_CHECK_EQ_U64(ev_counter(rt, 14), 1);              /* player_leave */
  mosaic_world_player_chat(w, rt, p3);
  MT_CHECK_EQ_U64(ev_counter(rt, 12), 1);              /* player_chat */
  u32 e1 = mosaic_world_entity_spawn(w, rt, 5);
  MT_CHECK_EQ_U64(ev_counter(rt, 8), 1);               /* entity_spawn */
  mosaic_world_entity_damage(w, rt, e1, 3);
  MT_CHECK_EQ_U64(ev_counter(rt, 4), 1);               /* entity_damage */
  mosaic_world_entity_kill(w, rt, e1);
  MT_CHECK_EQ_U64(ev_counter(rt, 5), 1);               /* entity_death:目录事件名 */
  mosaic_world_block_break(w, rt, 0, 1, 2, 3, 7);
  MT_CHECK_EQ_U64(ev_counter(rt, 0), 1);               /* block_break */
  mosaic_world_block_place(w, rt, 0, 4, 5, 6, 8);
  MT_CHECK_EQ_U64(ev_counter(rt, 2), 1);               /* block_place */
  mosaic_world_item_use(w, rt, p3, 100);
  MT_CHECK_EQ_U64(ev_counter(rt, 11), 1);              /* item_use */
  mosaic_world_item_craft(w, rt, p3, 200);
  MT_CHECK_EQ_U64(ev_counter(rt, 10), 1);              /* item_craft */
  u32 e2 = mosaic_world_entity_spawn(w, rt, 6);
  MT_CHECK_EQ_U64(ev_counter(rt, 8), 2);

  /* 生成器在标准目录上跑通(全部事件注册 → 无 NONE 跳过):
     1000 tick 后 tick 精确 1000;entity_tick ≥ 存活实体 × ticks(e2 存活,
     生成器 spawn 只增不减);MID 档各事件在动作计数上继续增长;LOW 修正档
     事件(toggle 两项 + ⚠️3 四项 + item_craft/weather/time)为固定种子
     0x1234 的精确契约值(见下,1.6) */
  u64 s = mosaic_world_step(w, rt, 1000);
  MT_CHECK(s >= 1000);                                 /* tick 每 tick ≥ 1 次执行 */
  MT_CHECK_EQ_U64(ev_counter(rt, 17), 1000);           /* tick == ticks */
  MT_CHECK(ev_counter(rt, 9) >= 1000);                 /* entity_tick ≥ 1 实体 × 1000 */
  MT_CHECK(ev_counter(rt, 0) >= 2);                    /* block_break:动作 1 + 生成器 MID ≥ 1 */
  MT_CHECK(ev_counter(rt, 2) >= 2);                    /* block_place */
  MT_CHECK(ev_counter(rt, 4) >= 2);                    /* entity_damage */
  MT_CHECK(ev_counter(rt, 11) >= 2);                   /* item_use */
  MT_CHECK(ev_counter(rt, 12) >= 2);                   /* player_chat */
  MT_CHECK(ev_counter(rt, 5) >= 2);                    /* entity_death:动作 1 + 生成器击杀 ≥ 1 */
  /* 生成器抽样事件精确契约值(1.6):固定种子 0x1234 的实测期望值,替代
     "≥ 1 命中"式种子实证断言(0.1% 事件在 1000 tick 窗口内可能零命中,
     旧断言依赖具体种子碰巧命中——脆弱)。rng 调用序是契约(勿重排):
     生成器/目录变更改变序列 → 本组值显式失败,须复核后同步更新。 */
  MT_CHECK_EQ_U64(ev_counter(rt, 15), 34);   /* player_toggle_sneak(5%) */
  MT_CHECK_EQ_U64(ev_counter(rt, 16), 53);   /* player_toggle_sprint(5%) */
  MT_CHECK_EQ_U64(ev_counter(rt, 1), 2);     /* block_explode(0.1%;生成 2) */
  MT_CHECK_EQ_U64(ev_counter(rt, 3), 3);     /* entity_combust(0.1%;生成 3) */
  MT_CHECK_EQ_U64(ev_counter(rt, 6), 0);     /* entity_explode(0.1%;窗口内零命中) */
  MT_CHECK_EQ_U64(ev_counter(rt, 7), 0);     /* entity_fall(0.1%;窗口内零命中) */
  MT_CHECK_EQ_U64(ev_counter(rt, 10), 2);    /* item_craft(0.1%;动作 1 + 生成 1) */
  MT_CHECK_EQ_U64(ev_counter(rt, 18), 0);    /* time_change(0.1%;窗口内零命中) */
  MT_CHECK_EQ_U64(ev_counter(rt, 19), 2);    /* weather_change(0.1%;生成 2) */
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 1000);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

/* ---- 1.5 HIGH 档门禁:目录每个 HIGH 事件必须被生成器覆盖表(g_freq_fixes)
     收录,或出现在显式排除清单中(理由见下)——目录新增 HIGH 事件若不在覆盖
     表,合成世界将静默不派发它(装饰性地雷)。排除清单条目同时校验其确为目录
     HIGH 事件(防拼写错误使门禁空转)。 ---- */
static void test_high_tier_gate(void) {
  static const char *EXCLUDED_HIGH[] = {
    /* 生成器不派发的目录 HIGH 事件(按域分组理由):
       - block_*(9,除 break/explode/place 已入表):燃烧/蔓延/红石/计划刻/物理
         等细粒度方块行为——合成世界无方块网格,不模拟;
       - chunk_load/unload:合成世界无区块网格;
       - entity_*(11,除 combust/damage/explode/fall/tick 已入表):成因伤害细分
         (damage_by_*)、进入方块/交互/药水/弹射物/回血/传送——无对应世界状态;
       - inventory_change:合成世界无背包模型;
       - packet_received/packet_sent(Task 6 网络域):合成世界无网络协议流
         (真实触发经 agent Connection 挂钩,见 MosaicHooks);
       - player_*(command_preprocess/interact/interact_at_entity/inventory_click/
         move/swing_arm):合成世界不模拟玩家输入流(玩家输入需客户端输入流,
         合成世界无玩家输入模型;聊天经 player_chat 已入 MID 档)。 */
    "block_burn", "block_from_to", "block_ignite", "block_interact",
    "block_moisture_change", "block_physics", "block_redstone", "block_spread",
    "block_tick",
    "chunk_load", "chunk_unload",
    "entity_block_form", "entity_change_block", "entity_damage_by_block",
    "entity_damage_by_entity", "entity_enter_block", "entity_interact",
    "entity_potion_effect", "entity_projectile_hit", "entity_projectile_launch",
    "entity_regain_health", "entity_teleport",
    "inventory_change",
    "packet_received", "packet_sent",
    "player_command_preprocess", "player_interact",
    "player_interact_at_entity", "player_inventory_click", "player_move",
    "player_swing_arm",
  };
  /* 目录每个 HIGH 事件 ∈ 覆盖表 ∪ 排除清单 */
  for (u32 i = 0; i < mosaic_events_catalog_count; i++) {
    const mosaic_ev_spec *s = &mosaic_events_catalog[i];
    if (s->freq != MOSAIC_EV_FREQ_HIGH) continue;
    int covered = 0;
    for (u32 k = 0; k < mosaic_world_gen_table_count(); k++)
      if (strcmp(s->name, mosaic_world_gen_table_name(k)) == 0) { covered = 1; break; }
    if (covered) continue;
    int excluded = 0;
    for (size_t k = 0; k < sizeof EXCLUDED_HIGH / sizeof EXCLUDED_HIGH[0]; k++)
      if (strcmp(s->name, EXCLUDED_HIGH[k]) == 0) { excluded = 1; break; }
    if (!excluded)   /* 失败前打印事件名,门禁失败可直接定位(F-4) */
      fprintf(stderr, "HIGH event not covered/excluded: %s\n", s->name);
    MT_CHECK(excluded);   /* 目录 HIGH 事件必须入表或显式排除 */
  }
  /* 排除清单条目必须真实存在于目录且为 HIGH(防拼写错误使门禁空转) */
  for (size_t k = 0; k < sizeof EXCLUDED_HIGH / sizeof EXCLUDED_HIGH[0]; k++) {
    const mosaic_ev_spec *s = mosaic_event_spec_by_name(EXCLUDED_HIGH[k]);
    MT_CHECK(s != NULL && s->freq == MOSAIC_EV_FREQ_HIGH);
  }
}

/* ---- 生成器确定性:同 seed + 同动作序列 + 同 step → 同计数与终态
     (step 返回值、全部订阅计数增量、世界终态一致;载荷流未断言) ---- */
static void test_generator_determinism(void) {
  mosaic_runtime *rt = open_pack_a();
  MT_CHECK(rt != NULL);
  if (!rt) return;
  const u32 seed = 0x12345678u;

  mosaic_world *w1 = mosaic_world_create(seed);
  MT_CHECK(w1 != NULL);
  if (!w1) { mosaic_runtime_close(rt); return; }
  mosaic_world_player_join(w1, rt);
  mosaic_world_entity_spawn(w1, rt, 3);
  mosaic_world_entity_spawn(w1, rt, 4);
  u64 r1 = mosaic_world_step(w1, rt, 500);
  u32 c1[14];
  for (u32 i = 0; i < 14; i++) c1[i] = ev_counter(rt, i);
  u32 ce1 = mosaic_world_entity_count(w1), cp1 = mosaic_world_player_count(w1);

  mosaic_world *w2 = mosaic_world_create(seed);
  MT_CHECK(w2 != NULL);
  if (!w2) { mosaic_world_destroy(w1); mosaic_runtime_close(rt); return; }
  mosaic_world_player_join(w2, rt);
  mosaic_world_entity_spawn(w2, rt, 3);
  mosaic_world_entity_spawn(w2, rt, 4);
  u64 r2 = mosaic_world_step(w2, rt, 500);
  MT_CHECK_EQ_U64(r2, r1);                              /* 派发总数一致 */
  for (u32 i = 0; i < 14; i++)
    MT_CHECK_EQ_U64(ev_counter(rt, i) - c1[i], c1[i]);  /* 每事件增量一致 */
  MT_CHECK_EQ_U64(mosaic_world_entity_count(w2), ce1);
  MT_CHECK_EQ_U64(mosaic_world_player_count(w2), cp1);
  MT_CHECK_EQ_U64(mosaic_world_ticks(w2), mosaic_world_ticks(w1));
  mosaic_world_destroy(w1);
  mosaic_world_destroy(w2);
  mosaic_runtime_close(rt);
}

/* ---- HIGH 档计数:干净窗口(无生成器 spawn/kill)内
      entity_tick 计数 == 实体数 × ticks;tick 计数 == ticks ---- */
static void test_generator_high_tier_counts(void) {
  mosaic_runtime *rt = open_pack_a();
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_world *w = mosaic_world_create(0x1u);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }
  mosaic_world_entity_spawn(w, rt, 1);
  mosaic_world_entity_spawn(w, rt, 2);
  mosaic_world_entity_spawn(w, rt, 3);
  u64 r = mosaic_world_step(w, rt, 16);
  /* 干净窗口断言(1.6 固定种子契约):固定种子 0x1 下窗口内生成器未
     spawn/kill,故 entity_tick == 3 × 16;rng 调用序是契约,若未来调整
     破坏该窗口,断言立即显式失败(须复核种子或改断言方式) */
  MT_CHECK_EQ_U64(ev_counter(rt, 4), 3);                /* 仅测试自身 3 次 spawn */
  MT_CHECK_EQ_U64(ev_counter(rt, 3), 0);                /* entity_death:窗口内 0 */
  MT_CHECK_EQ_U64(ev_counter(rt, 5), 3 * 16);           /* entity_tick == 实体数 × ticks */
  MT_CHECK_EQ_U64(ev_counter(rt, 11), 16);              /* tick */
  MT_CHECK(r >= 3 * 16 + 16);
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 16);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

/* ---- 生成器活跃与增长:自动补玩家(≥1 且小),实体数随 step 增长
     (LOW 档生成 0.2% > 击杀 0.1%),派发总数随 ticks 放大 ---- */
static void test_generator_liveness_growth(void) {
  mosaic_runtime *rt = open_pack_a();
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_world *w = mosaic_world_create(0xBEEFu);
  MT_CHECK(w != NULL);
  if (!w) { mosaic_runtime_close(rt); return; }
  mosaic_world_entity_spawn(w, rt, 2);
  u32 e_before = mosaic_world_entity_count(w);
  u64 r = mosaic_world_step(w, rt, 2000);
  MT_CHECK(mosaic_world_player_count(w) >= 1);          /* 自动补玩家:世界活跃 */
  MT_CHECK(mosaic_world_player_count(w) <= 10);         /* 且保持小(≤ 3 补向 + LOW 抽样) */
  MT_CHECK(mosaic_world_entity_count(w) >= e_before);   /* 实体数随 step 增长 */
  MT_CHECK(r >= 2000);                                  /* tick 事件每 tick ≥ 1 次执行 */
  MT_CHECK_EQ_U64(mosaic_world_ticks(w), 2000);
  mosaic_world_destroy(w);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 1; }
  SO_PATH = argv[1];
  MT_RUN(test_world_basics);
  MT_RUN(test_actions_dispatch_events);
  MT_RUN(test_payload_probe);
  MT_RUN(test_unregistered_events_noop);
  MT_RUN(test_standard_catalog_chain);
  MT_RUN(test_high_tier_gate);
  MT_RUN(test_generator_determinism);
  MT_RUN(test_generator_high_tier_counts);
  MT_RUN(test_generator_liveness_growth);
  return MT_RESULT() ? 0 : 1;
}
