#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;
static const char *MISSING_SO = "/tmp/definitely_missing.so";

static int build_pack(const char *path) {
  char err[256];
  /* 模块 A(10):fns 0,1 → 订阅 event0;模块 B(20):fn 0 → 订阅 event0 和 event1 */
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 2, 3, 4, 0, 2);   /* 4 条触发 */
  mosaic_pack_builder_add_event(b, "player_join");  /* 0 */
  mosaic_pack_builder_add_event(b, "block_break");  /* 1 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", SO_PATH);
  mosaic_pack_builder_add_module(b, 20, 1, "mod_b", MISSING_SO);   /* so 不存在 → 降级测试 */
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 1, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 20, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 1);
  mosaic_pack_builder_add_trigger(b, 0, 20ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 1, 20ull << 32 | 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_dispatch_executes_subscribers(void) {
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_trig.pack") == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_trig.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u32 ev_join = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK_EQ_U64(ev_join, 0);
  /* event0 有 3 个订阅,但 mod_b 的 so 缺失 → 降级跳过 → 执行 2 个 */
  u32 n = mosaic_event_dispatch(rt, ev_join, NULL);
  MT_CHECK_EQ_U64(n, 2);
  /* mod_a 两个函数已物化并各执行 1 次 */
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 1);      /* counter == 1 */
  MT_CHECK_EQ_U64(((u32 *)f0->state)[1], 0);  /* last_event == 0 */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (10ull << 32) | 1);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 1);
  /* 再次派发:ACTIVE 热路径,仍执行 2 个 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev_join, NULL), 2);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  /* 未知事件:0 个执行 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 999, NULL), 0);
  /* 未订阅事件(event1 只有 mod_b)→ 0 个执行(降级) */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 1, NULL), 0);
  mosaic_runtime_close(rt);
}

static void test_dispatch_tombstone_restore_cycle(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_trig.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 派发两次,然后墓碑,再派发(恢复路径) */
  mosaic_event_dispatch(rt, 0, NULL);
  mosaic_event_dispatch(rt, 0, NULL);
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  MT_CHECK(mosaic_fn_tombstone(rt, f0) == 0);
  MT_CHECK(mosaic_event_dispatch(rt, 0, NULL) == 2);   /* 重新物化/恢复 + 执行 */
  mosaic_fn_obj *f0b = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0b != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0b->state, 3);   /* 2 + 1,state 经 blob 恢复 */
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_dispatch_executes_subscribers);
  MT_RUN(test_dispatch_tombstone_restore_cycle);
  return MT_RESULT() ? 0 : 1;
}
