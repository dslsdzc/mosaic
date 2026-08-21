/* tests/test_tx.c — M2-2b:补丁 pack 事务 API(begin/validate/commit/rollback/
 * abort)+ 状态迁移 + 混合版本共存 + demote 回滚。
 *
 * base pack:模块 10(fn A = 10|0、fn B = 10|1,都是 code_inc,订阅 player_join)。
 * 补丁 pack:模块 10 v2、fn A 一条(code_off 4 = code_v2inc、gen 2、
 * set_fn_transform 1 = ×10);fn B 不在补丁 → 混合版本:B 天然走 v1。
 *
 * 覆盖:
 * 1. 完整生命周期:墓碑 v1 → commit(blob 迁移 3 → ×10 = 30)→ v2 执行 32 →
 *    B 仍 v1(1)→ demote → v1 恢复 3 → 执行 4;第二阶段:活对象在 ws 时
 *    commit(读活 state 4 → 40;quiesce 墓碑 A)→ 再 demote → 4。
 * 2. begin 拒绝路径:module not in base / fn not in base / generation not
 *    newer / event table mismatch / version regress。
 * 3. validate 拒绝路径:transform index out of range / abi probe failed /
 *    code_off out of range。
 * 4. abort 无副作用(begin 后 abort;validate 失败后 abort)。
 * 5. commit 后立即 demote(未物化 v2)。 */
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic/event.h"
#include "mosaic/tx.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *SO_PATH;    /* test_mod(+2 / transform ×10) */
static const char *V2_PATH;    /* test_mod_v2(+7 / transform ×10) */
static const char *V3_PATH;    /* test_mod_v3(+13 / transform ×10) */
#define BASE_PATH "/tmp/mosaic_tx_base.pack"
#define PATCH_OK_PATH "/tmp/mosaic_tx_patch_ok.pack"
#define PATCH_NOMOD_PATH "/tmp/mosaic_tx_patch_nomod.pack"
#define PATCH_NOFN_PATH "/tmp/mosaic_tx_patch_nofn.pack"
#define PATCH_GEN_PATH "/tmp/mosaic_tx_patch_gen.pack"
#define PATCH_EV_PATH "/tmp/mosaic_tx_patch_ev.pack"
#define PATCH_TX_PATH "/tmp/mosaic_tx_patch_tx.pack"
#define PATCH_VER_PATH "/tmp/mosaic_tx_patch_ver.pack"
#define PATCH_BADSO_PATH "/tmp/mosaic_tx_patch_badso.pack"
#define PATCH_CO_PATH "/tmp/mosaic_tx_patch_co.pack"
/* M2-2b 修复回归(C-1/I-1/I-2)专用补丁 pack */
#define PATCH_MC1_PATH "/tmp/mosaic_tx_patch_mc1.pack"   /* 二次 commit 迁移:gen2,so=test_mod */
#define PATCH_MC2_PATH "/tmp/mosaic_tx_patch_mc2.pack"   /* 二次 commit 迁移:gen3,so=test_mod */
#define PATCH_INV_PATH "/tmp/mosaic_tx_patch_inv.pack"   /* mods 缓存失效:so=test_mod_v2 */
#define PATCH_LS1_PATH "/tmp/mosaic_tx_patch_ls1.pack"   /* 最新补丁优先:patch1 so=test_mod_v2 */
#define PATCH_LS2_PATH "/tmp/mosaic_tx_patch_ls2.pack"   /* 最新补丁优先:patch2 so=test_mod_v3 */

#define A_ID (10ull << 32)
#define B_ID ((10ull << 32) | 1)
#define D_ID ((10ull << 32) | 2)   /* rollback 缓存失效回归:base 记录 code_off 4 */

#define FN_FLAGS (MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE)

static int build_base(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(BASE_PATH, 1, 2, 2, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 10, 1, "mod_10", SO_PATH);
  mosaic_pack_builder_add_fn(b, 10, 0, 0 /* code_inc */, 64, 1, 0, FN_FLAGS);
  mosaic_pack_builder_add_fn(b, 10, 1, 0 /* code_inc */, 64, 1, 0, FN_FLAGS);
  mosaic_pack_builder_add_trigger(b, 0, A_ID);
  mosaic_pack_builder_add_trigger(b, 0, B_ID);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build base: %s\n", err);
  return rc;
}

/* rollback 缓存失效回归专用 base:模块 10 三个函数——A/B 同 build_base
   (code_off 0),D 的 base 记录 code_off 4(test_mod 在该槽位是 code_v2inc
   +2;补丁 .so test_mod_v2 同槽位是 v2inc7 +7)——rollback 后物化 D 的首次
   执行即判别 mods 缓存是否已被失效(两 .so 只在 code_off 4 行为不同,
   A 的 code_off 0 两 .so 行为一致无法区分)。 */
static int build_base_rb(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(BASE_PATH, 1, 3, 2, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 10, 1, "mod_10", SO_PATH);
  mosaic_pack_builder_add_fn(b, 10, 0, 0 /* code_inc */, 64, 1, 0, FN_FLAGS);
  mosaic_pack_builder_add_fn(b, 10, 1, 0 /* code_inc */, 64, 1, 0, FN_FLAGS);
  mosaic_pack_builder_add_fn(b, 10, 2, 4 /* code_v2inc */, 64, 1, 0, FN_FLAGS);
  mosaic_pack_builder_add_trigger(b, 0, A_ID);
  mosaic_pack_builder_add_trigger(b, 0, B_ID);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build base_rb: %s\n", err);
  return rc;
}

/* 参数化补丁:module_id/version/so/local_id/code_off/gen/transform/事件对 */
static int build_patch_ex(const char *path, u64 module_id, u32 version, const char *so,
                          u64 local_id, u32 code_off, u32 gen, u32 transform,
                          const char *e0, const char *e1) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 1, 0, 0, 2);
  mosaic_pack_builder_add_event(b, e0);
  mosaic_pack_builder_add_event(b, e1);
  mosaic_pack_builder_add_module(b, module_id, version, "mod_patch", so);
  mosaic_pack_builder_add_fn(b, module_id, local_id, code_off, 64, gen, 0, FN_FLAGS);
  if (transform) {
    if (mosaic_pack_builder_set_fn_transform(b, (module_id << 32) | local_id, transform) != 0) {
      fprintf(stderr, "build %s: set_fn_transform failed\n", path);
      mosaic_pack_builder_free(b);
      return -1;
    }
  }
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build %s: %s\n", path, err);
  return rc;
}

static int build_patch_ok(void) {
  return build_patch_ex(PATCH_OK_PATH, 10, 2, SO_PATH, 0, 4, 2, 1, "player_join", "block_break");
}
static int build_patch_nomod(void) {   /* 模块 55 不在 base */
  return build_patch_ex(PATCH_NOMOD_PATH, 55, 2, SO_PATH, 0, 4, 2, 0, "player_join", "block_break");
}
static int build_patch_nofn(void) {    /* local 5 不在 base */
  return build_patch_ex(PATCH_NOFN_PATH, 10, 2, SO_PATH, 5, 0, 2, 0, "player_join", "block_break");
}
static int build_patch_gen(void) {     /* gen 1 == base 当前活跃代,不更新 */
  return build_patch_ex(PATCH_GEN_PATH, 10, 2, SO_PATH, 0, 4, 1, 0, "player_join", "block_break");
}
static int build_patch_ev(void) {      /* 事件表与 base 不一致 */
  return build_patch_ex(PATCH_EV_PATH, 10, 2, SO_PATH, 0, 4, 2, 0, "player_join", "craft");
}
static int build_patch_tx(void) {      /* transform 索引 99 越界(test_mod 仅 1 个) */
  return build_patch_ex(PATCH_TX_PATH, 10, 2, SO_PATH, 0, 4, 2, 99, "player_join", "block_break");
}
static int build_patch_ver(void) {     /* version 0 < base 1:版本回退 */
  return build_patch_ex(PATCH_VER_PATH, 10, 0, SO_PATH, 0, 4, 2, 0, "player_join", "block_break");
}
static int build_patch_badso(void) {   /* 补丁模块 so 不存在:ABI 探测失败 */
  return build_patch_ex(PATCH_BADSO_PATH, 10, 2, "/nonexistent/mosaic_tx.so", 0, 4, 2, 0,
                        "player_join", "block_break");
}
static int build_patch_co(void) {      /* code_off 9 ≥ abi->fn_count(5) */
  return build_patch_ex(PATCH_CO_PATH, 10, 2, SO_PATH, 0, 9, 2, 0, "player_join", "block_break");
}
/* ---- M2-2b 修复回归:补丁 pack 构建(事件表与 base 一致、模块同 id 10、
     version/generation 递增、so_path 参数化,复用 build_patch_ex) ---- */
static int build_patch_mc1(void) {     /* 二次 commit 迁移 patch1:gen2,so=test_mod,transform ×10 */
  return build_patch_ex(PATCH_MC1_PATH, 10, 2, SO_PATH, 0, 4, 2, 1, "player_join", "block_break");
}
static int build_patch_mc2(void) {     /* 二次 commit 迁移 patch2:gen3,so=test_mod,transform ×10 */
  return build_patch_ex(PATCH_MC2_PATH, 10, 3, SO_PATH, 0, 4, 3, 1, "player_join", "block_break");
}
static int build_patch_inv(void) {     /* mods 缓存失效:so=test_mod_v2(+7),gen2,transform ×10 */
  return build_patch_ex(PATCH_INV_PATH, 10, 2, V2_PATH, 0, 4, 2, 1, "player_join", "block_break");
}
static int build_patch_ls1(void) {     /* 最新补丁优先 patch1:so=test_mod_v2,gen2 */
  return build_patch_ex(PATCH_LS1_PATH, 10, 2, V2_PATH, 0, 4, 2, 1, "player_join", "block_break");
}
static int build_patch_ls2(void) {     /* 最新补丁优先 patch2:so=test_mod_v3,gen3 */
  return build_patch_ex(PATCH_LS2_PATH, 10, 3, V3_PATH, 0, 4, 3, 1, "player_join", "block_break");
}

static void check_begin_fail(const char *path, const char *expect) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_tx *tx = mosaic_tx_begin(rt, path, err, sizeof err);
  MT_CHECK(tx == NULL);
  if (tx) {
    mosaic_tx_free(tx);
  } else {
    MT_CHECK(strstr(err, expect) != NULL);
  }
  mosaic_runtime_close(rt);
}

static void check_validate_fail(const char *path, const char *expect) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_tx *tx = mosaic_tx_begin(rt, path, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == -1);
  MT_CHECK(strstr(err, expect) != NULL);
  mosaic_tx_abort(tx);
  mosaic_tx_free(tx);
  mosaic_runtime_close(rt);
}

/* ---- 1. 完整生命周期:墓碑 v1 → 迁移 ×10 → v2 执行 → 混合版本 → demote;
     第二阶段:活对象 commit(活 state 迁移 + quiesce)---- */
static void test_full_lifecycle(void) {
  char err[256];
  MT_CHECK(build_base() == 0 && build_patch_ok() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;

  /* v1:物化 A ×3 执行 → 3,墓碑(状态进 base blob) */
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  for (int i = 0; i < 3; i++) mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 3);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);

  /* 补丁事务:begin → validate → commit */
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx, err, sizeof err) == 0);

  /* v2:物化 A → blob 迁移 3 ×10 = 30;执行 ×1 → 32(code_v2inc += 2) */
  mosaic_fn_obj *v2 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(v2 != NULL);
  if (!v2) { mosaic_tx_free(tx); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)v2->state, 30);
  mosaic_fn_execute(v2, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)v2->state, 32);

  /* B 不在补丁 → 混合版本共存:仍走 v1(code_inc)→ 1 */
  mosaic_fn_obj *b1 = mosaic_fn_materialize(rt, B_ID);
  MT_CHECK(b1 != NULL);
  if (!b1) { mosaic_fn_tombstone(rt, v2); mosaic_tx_free(tx); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)b1->state, 0);
  mosaic_fn_execute(b1, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)b1->state, 1);

  /* 墓碑 v2 A(v2 状态 32 落补丁 blob),然后 rollback = demote */
  MT_CHECK(mosaic_fn_tombstone(rt, v2) == 0);
  MT_CHECK(mosaic_tx_rollback(tx, err, sizeof err) == 0);
  mosaic_tx_free(tx);

  /* v1 恢复:base blob 的 3(补丁 blob/路由已随 rollback 撤除);执行 → 4 */
  mosaic_fn_obj *v1 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(v1 != NULL);
  if (!v1) { mosaic_fn_tombstone(rt, b1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)v1->state, 3);
  mosaic_fn_execute(v1, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)v1->state, 4);

  /* 第二阶段:活对象在 ws(未墓碑)时 commit → 读活 state 4 → ×10 = 40;
     quiesce 墓碑 A(状态 4 回 base blob,保 v1 供 demote) */
  mosaic_tx *tx2 = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
  MT_CHECK(tx2 != NULL);
  if (!tx2) { mosaic_fn_tombstone(rt, v1); mosaic_fn_tombstone(rt, b1); mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx2, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx2, err, sizeof err) == 0);
  /* commit 的 quiesce 已墓碑 v1 对象(v1 指针此后不得再触碰) */
  mosaic_fn_obj *a2 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a2 != NULL);
  if (!a2) { mosaic_tx_free(tx2); mosaic_fn_tombstone(rt, b1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 40);
  mosaic_fn_execute(a2, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 42);
  MT_CHECK(mosaic_fn_tombstone(rt, a2) == 0);
  MT_CHECK(mosaic_tx_rollback(tx2, err, sizeof err) == 0);
  mosaic_tx_free(tx2);

  /* 再 demote:v1 恢复 quiesce 写的 4;执行 → 5 */
  mosaic_fn_obj *v1c = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(v1c != NULL);
  if (!v1c) { mosaic_fn_tombstone(rt, b1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)v1c->state, 4);
  mosaic_fn_execute(v1c, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)v1c->state, 5);

  /* 清理:墓碑全部活对象,LSAN 干净 */
  MT_CHECK(mosaic_fn_tombstone(rt, v1c) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, b1) == 0);
  mosaic_runtime_close(rt);
}

/* ---- 2. begin 拒绝路径 ---- */
static void test_begin_rejections(void) {
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_ok() == 0);
  MT_CHECK(build_patch_nomod() == 0);
  MT_CHECK(build_patch_nofn() == 0);
  MT_CHECK(build_patch_gen() == 0);
  MT_CHECK(build_patch_ev() == 0);
  MT_CHECK(build_patch_ver() == 0);
  check_begin_fail(PATCH_NOMOD_PATH, "tx module not in base");
  check_begin_fail(PATCH_NOFN_PATH, "tx fn not in base");
  check_begin_fail(PATCH_GEN_PATH, "tx generation not newer");
  check_begin_fail(PATCH_EV_PATH, "tx event table mismatch");
  check_begin_fail(PATCH_VER_PATH, "tx version regress");
  /* 合法补丁 begin 通过 + 事务释放 */
  {
    char err[256];
    mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
    MT_CHECK(rt != NULL);
    if (!rt) return;
    mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
    MT_CHECK(tx != NULL);
    if (tx) { mosaic_tx_abort(tx); mosaic_tx_free(tx); }
    mosaic_runtime_close(rt);
  }
}

/* ---- 3. validate 拒绝路径 ---- */
static void test_validate_rejections(void) {
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_tx() == 0);
  MT_CHECK(build_patch_badso() == 0);
  MT_CHECK(build_patch_co() == 0);
  check_validate_fail(PATCH_TX_PATH, "tx transform index out of range");
  check_validate_fail(PATCH_BADSO_PATH, "tx abi probe failed");
  check_validate_fail(PATCH_CO_PATH, "tx code_off out of range");
}

/* ---- 4. abort 无副作用 ---- */
static void test_abort_no_side_effects(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_ok() == 0);
  MT_CHECK(build_patch_tx() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* begin 后不 commit → abort → A 仍 v1 行为 */
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  mosaic_tx_abort(tx);
  mosaic_tx_free(tx);
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)f->state, 0);
  mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 1);   /* code_inc,非 v2inc */
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  /* validate 失败后 abort → 同样无副作用(恢复 1 → 执行 2) */
  mosaic_tx *tx2 = mosaic_tx_begin(rt, PATCH_TX_PATH, err, sizeof err);
  MT_CHECK(tx2 != NULL);
  if (!tx2) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx2, err, sizeof err) == -1);
  mosaic_tx_abort(tx2);
  mosaic_tx_free(tx2);
  mosaic_fn_obj *g = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(g != NULL);
  if (!g) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)g->state, 1);
  mosaic_fn_execute(g, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)g->state, 2);
  MT_CHECK(mosaic_fn_tombstone(rt, g) == 0);
  mosaic_runtime_close(rt);
}

/* ---- 5b. 派发路径:commit 后 trigger 派发路由到混合版本(事件 → 物化走
     find_function_active:A 是 v2、B 是 v1,一次派发各执行一次) ---- */
static void test_dispatch_mixed_versions(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_ok() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx, err, sizeof err) == 0);
  u32 ev = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK_EQ_U64(ev, 1);   /* 排序后:block_break=0, player_join=1 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev, NULL), 2);   /* A(v2) + B(v1) */
  mosaic_fn_obj *a = mosaic_fn_materialize(rt, A_ID);
  mosaic_fn_obj *b = mosaic_fn_materialize(rt, B_ID);
  MT_CHECK(a != NULL && b != NULL);
  if (!a || !b) {
    if (a) mosaic_fn_tombstone(rt, a);
    if (b) mosaic_fn_tombstone(rt, b);
    mosaic_tx_free(tx);
    mosaic_runtime_close(rt);
    return;
  }
  MT_CHECK_EQ_U64(*(u32 *)a->state, 2);   /* v2 初始零填充 → code_v2inc += 2 */
  MT_CHECK_EQ_U64(*(u32 *)b->state, 1);   /* v1 code_inc */
  MT_CHECK(mosaic_fn_tombstone(rt, a) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, b) == 0);
  MT_CHECK(mosaic_tx_rollback(tx, err, sizeof err) == 0);
  mosaic_tx_free(tx);
  mosaic_runtime_close(rt);
}

/* ---- 5. commit 后立即 demote(未物化 v2)---- */
static void test_commit_then_immediate_demote(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_ok() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_OK_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx, err, sizeof err) == 0);
  /* 未物化 v2 → rollback → 物化 A 走 v1(COLD 无 blob → 0 → 1) */
  MT_CHECK(mosaic_tx_rollback(tx, err, sizeof err) == 0);
  mosaic_tx_free(tx);
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)f->state, 0);
  mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 1);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  mosaic_runtime_close(rt);
}

/* ---- 6. C-1 回归:二次 commit 的状态迁移源(死路径)= 活跃记录。
     修复前:commit#2 的迁移读 base blob(3)→ 物化 = 30;修复后:读活跃
     记录(patch1 blob 32)→ ×10 = 320。 ---- */
static void test_tx_multi_commit_migration(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_mc1() == 0);
  MT_CHECK(build_patch_mc2() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* v1:物化 A ×3 → 3,墓碑(状态 3 进 base blob) */
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  for (int i = 0; i < 3; i++) mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 3);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  /* commit#1(gen2,transform ×10):死路径读 base blob 3 → 30 */
  mosaic_tx *tx1 = mosaic_tx_begin(rt, PATCH_MC1_PATH, err, sizeof err);
  MT_CHECK(tx1 != NULL);
  if (!tx1) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx1, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx1, err, sizeof err) == 0);
  mosaic_fn_obj *a1 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a1 != NULL);
  if (!a1) { mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a1->state, 30);
  mosaic_fn_execute(a1, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a1->state, 32);
  MT_CHECK(mosaic_fn_tombstone(rt, a1) == 0);   /* 活跃状态 32 → patch1 blob */
  /* commit#2(gen3):迁移必须从**活跃记录**(patch1 blob 32)迁移 → 320;
     修复前 find_function_ex 读 base blob 3 → 30 */
  mosaic_tx *tx2 = mosaic_tx_begin(rt, PATCH_MC2_PATH, err, sizeof err);
  MT_CHECK(tx2 != NULL);
  if (!tx2) { mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx2, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx2, err, sizeof err) == 0);
  mosaic_fn_obj *a2 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a2 != NULL);
  if (!a2) { mosaic_tx_free(tx2); mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 320);
  mosaic_fn_execute(a2, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 322);
  MT_CHECK(mosaic_fn_tombstone(rt, a2) == 0);
  /* 逆序 rollback(LIFO 纪律:先 tx2 后 tx1) */
  MT_CHECK(mosaic_tx_rollback(tx2, err, sizeof err) == 0);
  mosaic_tx_free(tx2);
  MT_CHECK(mosaic_tx_rollback(tx1, err, sizeof err) == 0);
  mosaic_tx_free(tx1);
  mosaic_runtime_close(rt);
}

/* ---- 7. I-1 回归:commit 使补丁模块的 mods 缓存失效。
     修复前:commit 后物化命中 mods 缓存(旧 test_mod)→ 执行 code_v2inc(+2)
     = 32;修复后:缓存失效 → 重新 dlopen 补丁 so(test_mod_v2)→ +7 = 37。
     物化 A 需在 commit 前已 ACTIVE(缓存已加载 test_mod)——commit 的
     quiesce 会墓碑它,故先墓碑再物化一次,让缓存命中且 A 存活。 ---- */
static void test_tx_commit_invalidates_mods(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_inv() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* v1:物化 A ×3 → 3 → 墓碑 → 物化 A 再次(3,mods 缓存命中 test_mod) */
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  for (int i = 0; i < 3; i++) mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 3);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)f->state, 3);
  /* 补丁1(so=test_mod_v2,gen2,code_off 4 = +7,transform ×10) */
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_INV_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_fn_tombstone(rt, f); mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx, err, sizeof err) == 0);
  /* commit 的 quiesce 已墓碑 f(此后不得再触碰);物化 A → 迁移 3 ×10 = 30;
     执行必须走新 .so(test_mod_v2 v2inc7 +7 = 37) */
  mosaic_fn_obj *a = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a != NULL);
  if (!a) { mosaic_tx_free(tx); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a->state, 30);
  mosaic_fn_execute(a, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a->state, 37);
  MT_CHECK(mosaic_fn_tombstone(rt, a) == 0);
  MT_CHECK(mosaic_tx_rollback(tx, err, sizeof err) == 0);
  mosaic_tx_free(tx);
  mosaic_runtime_close(rt);
}

/* ---- 7b. M2 遗留修复回归:rollback(demote)必须使补丁模块的 mods 缓存失效。
     修复前:rollback 后缓存仍持有补丁 .so(test_mod_v2)的 abi,base 路由的
     物化以 **base 记录 code_off** 执行补丁 .so 代码。A 的 base 记录
     code_off 0(两 .so 的 code_inc 行为一致,无法区分),故 base 另设 fn D
     (code_off 4):test_mod 该槽位 = code_v2inc(+2)、test_mod_v2 = v2inc7
     (+7)——rollback 后物化 D 的首次执行即暴露缓存是否失效。修复后重新
     dlopen base so(test_mod)→ +2 = 2;修复前缓存命中补丁 so → +7 = 7
     (断言 2 != 7 区分)。A 侧数值(3/30/37/4)与 I-1 回归一致。 ---- */
static void test_tx_rollback_invalidates_mods(void) {
  char err[256];
  MT_CHECK(build_base_rb() == 0);
  MT_CHECK(build_patch_inv() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* v1:物化 A ×3 → 3,墓碑(状态 3 进 base blob;mods 缓存加载 test_mod) */
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  for (int i = 0; i < 3; i++) mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 3);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  /* 补丁1(so=test_mod_v2,gen2,code_off 4 = +7,transform ×10) */
  mosaic_tx *tx = mosaic_tx_begin(rt, PATCH_INV_PATH, err, sizeof err);
  MT_CHECK(tx != NULL);
  if (!tx) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx, err, sizeof err) == 0);
  /* commit 的 quiesce 已墓碑 f;物化 A → 迁移 3 ×10 = 30;执行 → 37
     (补丁记录 code_off 4 → test_mod_v2 +7);缓存:module 10 → test_mod_v2 */
  mosaic_fn_obj *a = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a != NULL);
  if (!a) { mosaic_tx_free(tx); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a->state, 30);
  mosaic_fn_execute(a, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a->state, 37);
  MT_CHECK(mosaic_fn_tombstone(rt, a) == 0);
  /* rollback = demote:路由回 base、补丁 pack 撤除、mods 缓存必须失效 */
  MT_CHECK(mosaic_tx_rollback(tx, err, sizeof err) == 0);
  mosaic_tx_free(tx);
  /* v1 恢复:物化 A → base blob 3;执行 → 4(base 记录 code_off 0 → code_inc) */
  mosaic_fn_obj *v1 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(v1 != NULL);
  if (!v1) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)v1->state, 3);
  mosaic_fn_execute(v1, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)v1->state, 4);
  MT_CHECK(mosaic_fn_tombstone(rt, v1) == 0);
  /* 判别:物化 base 路由的 D(base 记录 code_off 4)。修复后:缓存已失效 →
     mod_load 按 base 记录 so_path 重新 dlopen test_mod → code_v2inc(+2)= 2;
     修复前:缓存命中补丁 .so test_mod_v2 → v2inc7(+7)= 7。断言 2 != 7。 */
  mosaic_fn_obj *d = mosaic_fn_materialize(rt, D_ID);
  MT_CHECK(d != NULL);
  if (!d) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)d->state, 0);
  mosaic_fn_execute(d, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)d->state, 2);
  MT_CHECK(mosaic_fn_tombstone(rt, d) == 0);
  mosaic_runtime_close(rt);
}

/* ---- 8. I-2 回归:多补丁下 find_module_active 必须解析到**最新**补丁的
     so_path。修复前顺序扫描 tx_packs 返回 patch1 记录(so=test_mod_v2)→
     执行 +7 = 177;修复后反向扫描返回 patch2 记录(so=test_mod_v3)→
     +13 = 183。同时验证二次迁移源(C-1):17 → ×10 = 170。 ---- */
static void test_tx_multi_patch_latest_so(void) {
  char err[256];
  MT_CHECK(build_base() == 0);
  MT_CHECK(build_patch_ls1() == 0);
  MT_CHECK(build_patch_ls2() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(BASE_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* v1:物化 A ×1 → 1,墓碑(状态 1 进 base blob) */
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(f != NULL);
  if (!f) { mosaic_runtime_close(rt); return; }
  mosaic_fn_execute(f, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)f->state, 1);
  MT_CHECK(mosaic_fn_tombstone(rt, f) == 0);
  /* 补丁1(so=test_mod_v2,gen2):迁移 1 ×10 = 10;执行 +7 = 17 */
  mosaic_tx *tx1 = mosaic_tx_begin(rt, PATCH_LS1_PATH, err, sizeof err);
  MT_CHECK(tx1 != NULL);
  if (!tx1) { mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx1, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx1, err, sizeof err) == 0);
  mosaic_fn_obj *a1 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a1 != NULL);
  if (!a1) { mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a1->state, 10);
  mosaic_fn_execute(a1, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a1->state, 17);
  MT_CHECK(mosaic_fn_tombstone(rt, a1) == 0);   /* 活跃状态 17 → patch1 blob */
  /* 补丁2(so=test_mod_v3,gen3):迁移活跃 17 ×10 = 170 */
  mosaic_tx *tx2 = mosaic_tx_begin(rt, PATCH_LS2_PATH, err, sizeof err);
  MT_CHECK(tx2 != NULL);
  if (!tx2) { mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK(mosaic_tx_validate(tx2, err, sizeof err) == 0);
  MT_CHECK(mosaic_tx_commit(tx2, err, sizeof err) == 0);
  mosaic_fn_obj *a2 = mosaic_fn_materialize(rt, A_ID);
  MT_CHECK(a2 != NULL);
  if (!a2) { mosaic_tx_free(tx2); mosaic_tx_free(tx1); mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 170);
  /* 执行必须走**最新**补丁 so:test_mod_v3 v3inc13 → 183;
     修复前拿 patch1 的 test_mod_v2 so_path → 177 */
  mosaic_fn_execute(a2, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)a2->state, 183);
  MT_CHECK(mosaic_fn_tombstone(rt, a2) == 0);
  MT_CHECK(mosaic_tx_rollback(tx2, err, sizeof err) == 0);
  mosaic_tx_free(tx2);
  MT_CHECK(mosaic_tx_rollback(tx1, err, sizeof err) == 0);
  mosaic_tx_free(tx1);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <test_mod.so> <test_mod_v2.so> <test_mod_v3.so>\n", argv[0]);
    return 2;
  }
  SO_PATH = argv[1];
  V2_PATH = argv[2];
  V3_PATH = argv[3];
  MT_RUN(test_full_lifecycle);
  MT_RUN(test_begin_rejections);
  MT_RUN(test_validate_rejections);
  MT_RUN(test_abort_no_side_effects);
  MT_RUN(test_dispatch_mixed_versions);
  MT_RUN(test_commit_then_immediate_demote);
  MT_RUN(test_tx_multi_commit_migration);
  MT_RUN(test_tx_commit_invalidates_mods);
  MT_RUN(test_tx_rollback_invalidates_mods);
  MT_RUN(test_tx_multi_patch_latest_so);
  return MT_RESULT() ? 0 : 1;
}
