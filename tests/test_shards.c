/* tests/test_shards.c — M1.5-A:运行时多 pack 合并支持
   三个 pack:P0(模块 10/20)、P1(模块 30)、P2(模块 40),事件集全部为
   player_join/block_break(与既有测试 pack 相同事件集),模块范围互不重叠。
   fn_id = (module_id << 32) | local,module_id 全局唯一 → fn_id 空间无冲突。 */
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"   /* find_function_ex / pack_map / rt->packs 断言 */
#include "mini_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

static const char *SO_PATH;
#define P0_PATH "/tmp/mosaic_shard_p0.pack"
#define P1_PATH "/tmp/mosaic_shard_p1.pack"
#define P2_PATH "/tmp/mosaic_shard_p2.pack"
#define OVL_PATH "/tmp/mosaic_shard_ovl.pack"
#define BAD_PATH "/tmp/mosaic_shard_bad.pack"

/* P0:模块 10(fns 0,1)+ 模块 20(fn 0);P1:模块 30(fn 0);P2:模块 40(fn 0)。
   订阅:player_join → 10|0, 10|1, 20|0, 30|0, 40|0(共 5);block_break → 20|0。 */
static int build_p0(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(P0_PATH, 2, 3, 4, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");   /* 注册 id 0 */
  mosaic_pack_builder_add_event(b, "block_break");   /* 注册 id 1 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod_10", SO_PATH);
  mosaic_pack_builder_add_module(b, 20, 1, "mod_20", SO_PATH);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 1, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 20, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 1);
  mosaic_pack_builder_add_trigger(b, 0, 20ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 1, 20ull << 32 | 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build p0: %s\n", err);
  return rc;
}

static int build_p1(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(P1_PATH, 1, 1, 1, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 30, 1, "mod_30", SO_PATH);
  mosaic_pack_builder_add_fn(b, 30, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 30ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build p1: %s\n", err);
  return rc;
}

static int build_p2(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(P2_PATH, 1, 1, 1, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 40, 1, "mod_40", SO_PATH);
  mosaic_pack_builder_add_fn(b, 40, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 40ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build p2: %s\n", err);
  return rc;
}

/* 拒绝路径 1:模块范围 15 落在 P0 的 10-20 内(事件集相同,隔离重叠判定) */
static int build_ovl(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(OVL_PATH, 1, 1, 0, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 15, 1, "mod_15", SO_PATH);
  mosaic_pack_builder_add_fn(b, 15, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build ovl: %s\n", err);
  return rc;
}

/* 拒绝路径 2:事件集不同(模块范围 50 不与 P0 重叠,隔离事件判定) */
static int build_bad(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(BAD_PATH, 1, 1, 0, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "craft");
  mosaic_pack_builder_add_module(b, 50, 1, "mod_50", SO_PATH);
  mosaic_pack_builder_add_fn(b, 50, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build bad: %s\n", err);
  return rc;
}

static void test_open_many_cross_pack_lookup(void) {
  char err[256];
  MT_CHECK(build_p0() == 0 && build_p1() == 0 && build_p2() == 0);
  /* 乱序传入:P2, P1, P0 → open 时须按 min 重建排序的 pack 视图 */
  const char *paths[3] = { P2_PATH, P1_PATH, P0_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 3, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) { fprintf(stderr, "open_many: %s\n", err); return; }
  /* 排序后的范围表:min 升序 10 / 30 / 40 */
  MT_CHECK_EQ_U64(rt->packs[0].min_mod, 10);
  MT_CHECK_EQ_U64(rt->packs[1].min_mod, 30);
  MT_CHECK_EQ_U64(rt->packs[2].min_mod, 40);
  /* find_module 跨 pack 命中;范围间隙命中不到 */
  MT_CHECK(mosaic_runtime_find_module(rt, 10) != NULL);
  MT_CHECK(mosaic_runtime_find_module(rt, 20) != NULL);
  MT_CHECK(mosaic_runtime_find_module(rt, 30) != NULL);
  MT_CHECK(mosaic_runtime_find_module(rt, 40) != NULL);
  MT_CHECK(mosaic_runtime_find_module(rt, 25) == NULL);
  /* find_function_ex 输出归属 pack(排序后下标) */
  size_t pk = 999;
  const mosaic_function_record *r;
  r = find_function_ex(rt, 10ull << 32, &pk);
  MT_CHECK(r != NULL); MT_CHECK_EQ_U64(pk, 0);
  r = find_function_ex(rt, 30ull << 32, &pk);
  MT_CHECK(r != NULL); MT_CHECK_EQ_U64(pk, 1);
  r = find_function_ex(rt, 40ull << 32, &pk);
  MT_CHECK(r != NULL); MT_CHECK_EQ_U64(pk, 2);
  /* function_count 跨 pack 求和:2 + 1 + 1 + 1 = 5 */
  MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), 5);
  /* 事件命名空间全局一致(pack 0 二分) */
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_join"), 1);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "block_break"), 0);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "nope"), MOSAIC_U32_NONE);
  mosaic_runtime_close(rt);
}

static void test_dispatch_across_packs(void) {
  char err[256];
  MT_CHECK(build_p0() == 0 && build_p1() == 0 && build_p2() == 0);
  const char *paths[3] = { P0_PATH, P1_PATH, P2_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 3, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u32 ev = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK_EQ_U64(ev, 1);   /* 排序后:block_break=0, player_join=1 */
  /* 三个 pack 的订阅者全部执行:10|0, 10|1, 20|0, 30|0, 40|0 → 5 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev, NULL), 5);
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  if (f0) MT_CHECK_EQ_U64(*(u32 *)f0->state, 1);
  mosaic_fn_obj *f30 = mosaic_fn_materialize(rt, 30ull << 32);
  MT_CHECK(f30 != NULL);
  if (f30) MT_CHECK_EQ_U64(*(u32 *)f30->state, 1);
  mosaic_fn_obj *f40 = mosaic_fn_materialize(rt, 40ull << 32);
  MT_CHECK(f40 != NULL);
  if (f40) MT_CHECK_EQ_U64(*(u32 *)f40->state, 1);
  /* 热路径再派发 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev, NULL), 5);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  /* 未知事件 id:0 个执行 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 999, NULL), 0);
  /* block_break 仅 20|0 订阅(已执行 2 次 player_join)→ 本次 1 次 → counter 3 */
  u32 eb = mosaic_runtime_event_id(rt, "block_break");
  MT_CHECK_EQ_U64(eb, 0);
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, eb, NULL), 1);
  mosaic_fn_obj *f20 = mosaic_fn_materialize(rt, 20ull << 32);
  MT_CHECK(f20 != NULL);
  if (f20) MT_CHECK_EQ_U64(*(u32 *)f20->state, 3);
  /* 清理:墓碑全部物化函数(派发物化了 10|0, 10|1, 20|0, 30|0, 40|0),
     state 回写 blob,保持 LSAN 干净 */
  if (f0) MT_CHECK(mosaic_fn_tombstone(rt, f0) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, mosaic_fn_materialize(rt, (10ull << 32) | 1)) == 0);
  if (f20) MT_CHECK(mosaic_fn_tombstone(rt, f20) == 0);
  if (f30) MT_CHECK(mosaic_fn_tombstone(rt, f30) == 0);
  if (f40) MT_CHECK(mosaic_fn_tombstone(rt, f40) == 0);
  mosaic_runtime_close(rt);
}

static void test_tombstone_restore_on_shard(void) {
  char err[256];
  /* 输入顺序 [P1, P0, P2]:P1 最先 mmap → 映射位于 mmap 区顶部,紧邻上方空闲;
     在 P1(排序后下标 1)映射末端放 PROT_NONE fence,强迫其 state blob 扩容
     mremap 必须搬家——验证"只动本 pack 映射,其余 pack 指针不受影响"。 */
  const char *paths[3] = { P1_PATH, P0_PATH, P2_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 3, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  long pg = sysconf(_SC_PAGESIZE);
  uintptr_t end = (uintptr_t)pack_map(rt, 1) + rt->packs[1].map_len;
  uintptr_t fstart = (end + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
  void *fence = mmap((void *)fstart, (size_t)pg, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (fence == MAP_FAILED) {
    /* ASan 等环境下目标地址可能被影子内存/allocator 占用:fence 只是"确定性
       逼搬家"的手段,不是测试目的本身。没有 fence 时 mremap 原地扩容或搬家
       均可——下述断言(重取指针、他 pack 指针稳定)两种情形都成立。 */
  }
  /* 先取 pack 0 的记录指针(墓碑后必须仍有效) */
  size_t pk = 999;
  const mosaic_function_record *rec_p0 = find_function_ex(rt, 10ull << 32, &pk);
  MT_CHECK(rec_p0 != NULL && pk == 0);
  if (!rec_p0) { munmap(fence, (size_t)pg); mosaic_runtime_close(rt); return; }
  /* P1 的函数:物化 → 3 次执行 → 墓碑(触发 P1 state blob 扩容 mremap,被 fence 逼搬家) */
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, 30ull << 32);
  MT_CHECK(fn != NULL);
  if (!fn) { munmap(fence, (size_t)pg); mosaic_runtime_close(rt); return; }
  for (int i = 0; i < 3; i++) mosaic_fn_execute(fn, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 3);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == 0);
  /* P0 的记录指针不受 P1 mremap 影响(分片 mremap 只动本 pack) */
  MT_CHECK_EQ_U64(mf_id(rec_p0), 10ull << 32);
  /* P1 的记录可从(搬家后的)新映射重取 */
  pk = 999;
  const mosaic_function_record *r30 = find_function_ex(rt, 30ull << 32, &pk);
  MT_CHECK(r30 != NULL && pk == 1);
  if (r30) {
    MT_CHECK_EQ_U64(mf_flags(r30) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);
    MT_CHECK(mf_state_off(r30) != 0);   /* state 已写入 P1 的 blob */
  }
  /* 跨 pack 派发:5 个订阅者,30|0 从 P1 blob 恢复 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, mosaic_runtime_event_id(rt, "player_join"), NULL), 5);
  mosaic_fn_obj *f2 = mosaic_fn_materialize(rt, 30ull << 32);
  MT_CHECK(f2 != NULL);
  if (f2) MT_CHECK_EQ_U64(*(u32 *)f2->state, 4);   /* 3 + 1 */
  if (fence != MAP_FAILED) munmap(fence, (size_t)pg);
  if (f2) MT_CHECK(mosaic_fn_tombstone(rt, f2) == 0);
  /* 清理:派发物化的其余 4 个订阅者也墓碑(materialize 幂等返回 ACTIVE 对象),
     保持 LSAN 干净 */
  MT_CHECK(mosaic_fn_tombstone(rt, mosaic_fn_materialize(rt, 10ull << 32)) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, mosaic_fn_materialize(rt, (10ull << 32) | 1)) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, mosaic_fn_materialize(rt, 20ull << 32)) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, mosaic_fn_materialize(rt, 40ull << 32)) == 0);
  mosaic_runtime_close(rt);
}

static void test_reject_overlapping_ranges(void) {
  char err[256];
  MT_CHECK(build_ovl() == 0);
  const char *paths[2] = { P0_PATH, OVL_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 2, err, sizeof err);
  MT_CHECK(rt == NULL);
  if (rt) { mosaic_runtime_close(rt); return; }
  MT_CHECK(strstr(err, "overlapping pack module ranges") != NULL);
  /* 单 pack open 不受影响(既有入口) */
  mosaic_runtime *r0 = mosaic_runtime_open(P0_PATH, err, sizeof err);
  MT_CHECK(r0 != NULL);
  if (r0) mosaic_runtime_close(r0);
}

static void test_reject_event_mismatch(void) {
  char err[256];
  MT_CHECK(build_bad() == 0);
  const char *paths[2] = { P0_PATH, BAD_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 2, err, sizeof err);
  MT_CHECK(rt == NULL);
  if (rt) { mosaic_runtime_close(rt); return; }
  MT_CHECK(strstr(err, "event table mismatch") != NULL);
}

static void test_no_packs_and_single(void) {
  char err[256];
  MT_CHECK(mosaic_runtime_open_many(NULL, 0, err, sizeof err) == NULL);
  MT_CHECK(strstr(err, "no packs") != NULL);
  const char *one[1] = { P0_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(one, 1, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), 3);   /* P0: 2 + 1 */
  MT_CHECK(mosaic_runtime_find_function(rt, 10ull << 32) != NULL);
  MT_CHECK(mosaic_runtime_find_function(rt, 20ull << 32) != NULL);
  MT_CHECK(mosaic_runtime_find_function(rt, 30ull << 32) == NULL);   /* 不在 P0 */
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_open_many_cross_pack_lookup);
  MT_RUN(test_dispatch_across_packs);
  MT_RUN(test_tombstone_restore_on_shard);
  MT_RUN(test_reject_overlapping_ranges);
  MT_RUN(test_reject_event_mismatch);
  MT_RUN(test_no_packs_and_single);
  return MT_RESULT() ? 0 : 1;
}
