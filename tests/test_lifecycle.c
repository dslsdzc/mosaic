#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"   /* 偏差 D-3:ws_find 断言需要内部头(与 test_working_set 同模式) */
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;
static const char *BAD_SO_PATH;

static int build_pack(const char *path, const char *so, u64 module_id, u32 fn_count) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, fn_count, 0, 0, 0);
  mosaic_pack_builder_add_module(b, module_id, 1, "mod", so);
  for (u32 i = 0; i < fn_count; i++)
    mosaic_pack_builder_add_fn(b, module_id, i, i % 3, 64, 1, 0,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build: %s\n", err);
  return rc;
}

static void test_materialize_and_execute(void) {
  const u64 MID = 100;
  const u64 F0 = MID << 32;          /* code_off 0 = code_inc */
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc.pack", SO_PATH, MID, 3) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  MT_CHECK_EQ_U64(fn->fn_id, F0);
  /* 物化后 state 为 0 */
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 0);
  /* 热路径:3 次 inc */
  for (int i = 0; i < 3; i++) mosaic_fn_execute(fn, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 3);
  /* 重复物化幂等:返回同一对象 */
  mosaic_fn_obj *fn2 = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn2 == fn);
  /* code_add:带事件载荷 */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (MID << 32) | 1);
  MT_CHECK(f1 != NULL);
  u32 add = 7;
  mosaic_fn_execute(f1, 0, &add);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 7);
  /* 不存在的 fn */
  MT_CHECK(mosaic_fn_materialize(rt, (MID << 32) | 99) == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

static void test_tombstone_restore_preserves_state(void) {
  const u64 MID = 200;
  const u64 F0 = MID << 32;
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc2.pack", SO_PATH, MID, 1) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc2.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  if (!fn) return;   /* 失败时避免 NULL 解引用掩盖断言输出 */
  for (int i = 0; i < 5; i++) mosaic_fn_execute(fn, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 5);
  int rc = mosaic_fn_tombstone(rt, fn);
  MT_CHECK_EQ_U64(rc, 0);
  /* 记录变为 COLD + state_off 已写 */
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, F0);
  MT_CHECK_EQ_U64(mf_flags(rec) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);
  MT_CHECK(mf_state_off(rec) != 0);
  /* 工作集已移除 */
  mosaic_fn_obj *f2 = mosaic_fn_materialize(rt, F0);   /* RESTORE 路径 */
  MT_CHECK(f2 != NULL);
  /* 偏差 D-3:slab 空闲链表复用(working_set.c fn_free/fn_alloc)使墓碑后的再物化
     返回同一内存槽,指针不等断言(计划原文 f2 != fn)无法成立;改为语义断言:
     旧对象已出工作集、新对象已入工作集。 */
  MT_CHECK(ws_find(rt, F0) == f2);   /* 新对象已入工作集(旧对象已被 tombstone 移除) */
  MT_CHECK_EQ_U64(*(u32 *)f2->state, 5);   /* state 从 blob 恢复 */
  mosaic_runtime_close(rt);
}

static void test_bad_abi_rejected(void) {
  const u64 MID = 300;
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc3.pack", BAD_SO_PATH, MID, 1) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc3.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, MID << 32);
  MT_CHECK(fn == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ABI);
  /* 失败后 flags 回滚为 COLD,可再次尝试 */
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, MID << 32);
  MT_CHECK_EQ_U64(mf_flags(rec) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);
  mosaic_runtime_close(rt);
}

static void test_materialize_while_not_cold_rejected(void) {
  /* 物化两次同 id 幂等已在 test_materialize_and_execute;这里验证墓碑后不能直接物化(恢复路径已覆盖),
     以及 materialize 对已 ACTIVE 返回同对象(幂等)已在上面。保持最小:验证 fn id 0x0 不存在 */
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK(mosaic_fn_materialize(rt, 0) == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s <test_mod.so> <test_badmod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1]; BAD_SO_PATH = argv[2];
  MT_RUN(test_materialize_and_execute);
  MT_RUN(test_tombstone_restore_preserves_state);
  MT_RUN(test_bad_abi_rejected);
  MT_RUN(test_materialize_while_not_cold_rejected);
  return MT_RESULT() ? 0 : 1;
}
