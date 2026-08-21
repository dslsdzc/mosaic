#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>

static const char *SO_PATH;
static const char *MISSING_SO = "/tmp/definitely_missing.so";

static int build_pack(const char *path) {
  char err[256];
  /* 模块 A(10):fns 0,1 → 订阅 player_join;模块 B(20):fn 0 → 订阅 player_join 和 block_break。
     add_trigger 用注册顺序 id(0/1),builder 在 finish 时重映射为排序后 id
     (排序后:block_break=0, player_join=1) */
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 2, 3, 4, 0, 2);   /* 4 条触发 */
  mosaic_pack_builder_add_event(b, "player_join");  /* 注册 id 0 */
  mosaic_pack_builder_add_event(b, "block_break");  /* 注册 id 1 */
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
  MT_CHECK_EQ_U64(ev_join, 1);   /* 排序位置:block_break=0, player_join=1 */
  /* player_join 有 3 个订阅,但 mod_b 的 so 缺失 → 降级跳过 → 执行 2 个 */
  u32 n = mosaic_event_dispatch(rt, ev_join, NULL);
  MT_CHECK_EQ_U64(n, 2);
  /* mod_a 两个函数已物化并各执行 1 次 */
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 1);      /* counter == 1 */
  MT_CHECK_EQ_U64(((u32 *)f0->state)[1], ev_join);  /* last_event == 派发的事件 id(=1) */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (10ull << 32) | 1);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 1);
  /* 再次派发:ACTIVE 热路径,仍执行 2 个 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev_join, NULL), 2);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  /* 未知事件 id:0 个执行 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 999, NULL), 0);
  /* block_break 只有 mod_b 订阅(缺失)→ 0 个执行(降级) */
  u32 ev_break = mosaic_runtime_event_id(rt, "block_break");
  MT_CHECK_EQ_U64(ev_break, 0);
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev_break, NULL), 0);
  mosaic_runtime_close(rt);
}

static void test_dispatch_tombstone_restore_cycle(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_trig.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 派发两次,然后墓碑,再派发(恢复路径) */
  u32 ev_join = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK_EQ_U64(ev_join, 1);   /* 排序位置:block_break=0, player_join=1 */
  mosaic_event_dispatch(rt, ev_join, NULL);
  mosaic_event_dispatch(rt, ev_join, NULL);
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  MT_CHECK(mosaic_fn_tombstone(rt, f0) == 0);
  MT_CHECK(mosaic_event_dispatch(rt, ev_join, NULL) == 2);   /* 重新物化/恢复 + 执行 */
  mosaic_fn_obj *f0b = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0b != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0b->state, 3);   /* 2 + 1,state 经 blob 恢复 */
  mosaic_runtime_close(rt);
}

/* ---- 重入回归:派发循环内 mod 回调墓碑自身,内部 mremap(MAYMOVE) 移动
   pack 映射 → 循环缓存的天 trigger 表指针悬垂(SIGSEGV,单线程同崩)。
   回归测试通过紧贴映射尾端的 PROT_NONE fence 强迫 mremap 必须搬家,
   使悬垂读确定性复现。修复后 dispatch 每轮从 pack_map(rt, p) 重取表指针。 ---- */
static int g_hook_calls = 0;
static void self_tombstone_hook(void *rt_, void *fn_) {
  g_hook_calls++;
  mosaic_fn_tombstone((mosaic_runtime *)rt_, (mosaic_fn_obj *)fn_);   /* 派发中墓碑自身 */
}

static void test_dispatch_self_tombstone_reentrancy(void) {
  char err[256];
  /* 1 模块 2 函数都订阅 event0:fn0 = hook(code_off 3),fn1 = inc(code_off 0) */
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_reentrant.pack", 1, 2, 2, 0, 1);
  mosaic_pack_builder_add_event(b, "reenter");
  mosaic_pack_builder_add_module(b, 60, 1, "mod", SO_PATH);
  mosaic_pack_builder_add_fn(b, 60, 0, 3, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 60, 1, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 60ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 60ull << 32 | 1);
  if (mosaic_pack_builder_finish(b, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); }
  mosaic_pack_builder_free(b);

  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_reentrant.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;

  /* 在 pack 映射尾端紧邻放置 PROT_NONE fence,强迫后续 mremap 移动映射(确定性复现) */
  long pg = sysconf(_SC_PAGESIZE);
  uintptr_t end = (uintptr_t)pack_map(rt, 0) + rt->packs[0].map_len;   /* M1.5-A:单 pack = pack 0 */
  uintptr_t fstart = (end + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
  void *fence = mmap((void *)fstart, (size_t)pg, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  MT_CHECK(fence != MAP_FAILED);
  MT_CHECK_EQ_U64((uintptr_t)fence, fstart);   /* 必须真实落在目标地址,否则 fence 无效 */

  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 60ull << 32);
  MT_CHECK(f0 != NULL);
  /* fn1 先物化:模块 refs=2,fn0 自墓碑时 mod_unload 只减引用不 dlclose——
     否则正在执行中的 .so 被卸载,code_hook 的返回地址落入已 unmap 代码页
     (SIGSEGV),测试永远走不到被验证的派发重入路径 */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (60ull << 32) | 1);
  MT_CHECK(f1 != NULL);
  /* 注意:sizeof 作用于函数名是 GNU 扩展(值恒为 1),直接 sizeof 只能拷贝
     1 字节(函数首条指令,如 push %rbp=0x55)→ hook 指针损坏;必须经指针
     变量取 sizeof(8) */
  void *hook_p = (void *)(uintptr_t)self_tombstone_hook;
  memcpy(f0->state, &rt, sizeof rt);
  memcpy((u8 *)f0->state + 8, &f0, sizeof f0);
  memcpy((u8 *)f0->state + 16, &hook_p, sizeof hook_p);

  u32 ev = mosaic_runtime_event_id(rt, "reenter");
  g_hook_calls = 0;
  u32 n = mosaic_event_dispatch(rt, ev, NULL);
  /* fn0 执行中自墓碑(触发 mremap 移动);循环必须继续派发 fn1 */
  MT_CHECK_EQ_U64(n, 2);
  MT_CHECK_EQ_U64(g_hook_calls, 1);
  const mosaic_function_record *r0 = mosaic_runtime_find_function(rt, 60ull << 32);
  MT_CHECK_EQ_U64(mf_flags(r0) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);   /* 已墓碑 */
  MT_CHECK(mf_state_off(r0) != 0);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 1);        /* fn1 正常执行过一次 */

  munmap(fence, (size_t)pg);
  mosaic_runtime_close(rt);
}

/* ---- 缺陷 2 回归:refs==1 时自墓碑不得立即 dlclose 正在执行的 .so ----
   与 test_dispatch_self_tombstone_reentrancy 互补:该用例 fn1 先物化(refs=2),
   自墓碑只减引用;本用例只有 fn0 物化(refs=1),自墓碑使 refs 归零进入 pending
   ——旧实现此处立即 dlclose,正在执行中的 .so 被卸载,code_hook 的返回地址
   落入已 unmap 代码页 → 返回即崩。修复:延迟到 dispatch 末尾统一 flush。 ---- */
static void test_self_tombstone_single_ref(void) {
  char err[256];
  /* 1 模块 2 函数,只有 fn0 订阅 event0:fn0 = hook(code_off 3),fn1 = inc(code_off 0) */
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_singletomb.pack", 1, 2, 1, 0, 1);
  mosaic_pack_builder_add_event(b, "single_tomb");
  mosaic_pack_builder_add_module(b, 70, 1, "mod", SO_PATH);
  mosaic_pack_builder_add_fn(b, 70, 0, 3, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 70, 1, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 70ull << 32 | 0);   /* 只订阅 fn0 */
  if (mosaic_pack_builder_finish(b, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); }
  mosaic_pack_builder_free(b);

  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_singletomb.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;

  /* PROT_NONE fence(同重入用例模式):墓碑时 state_blob_append 的 mremap 必须
     搬家移动映射;配合延迟 dlclose 验证执行中 .so 不卸载、返回地址不悬垂 */
  long pg = sysconf(_SC_PAGESIZE);
  uintptr_t end = (uintptr_t)pack_map(rt, 0) + rt->packs[0].map_len;
  uintptr_t fstart = (end + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
  void *fence = mmap((void *)fstart, (size_t)pg, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  MT_CHECK(fence != MAP_FAILED);
  MT_CHECK_EQ_U64((uintptr_t)fence, fstart);

  /* 只物化 fn0:模块 refs == 1。自墓碑 → refs 归零 → pending(不 dlclose) */
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 70ull << 32);
  MT_CHECK(f0 != NULL);
  if (!f0) { munmap(fence, (size_t)pg); mosaic_runtime_close(rt); return; }
  void *hook_p = (void *)(uintptr_t)self_tombstone_hook;
  memcpy(f0->state, &rt, sizeof rt);
  memcpy((u8 *)f0->state + 8, &f0, sizeof f0);
  memcpy((u8 *)f0->state + 16, &hook_p, sizeof hook_p);

  u32 ev = mosaic_runtime_event_id(rt, "single_tomb");
  g_hook_calls = 0;
  u32 n = mosaic_event_dispatch(rt, ev, NULL);
  /* 唯一订阅者 fn0 执行中自墓碑;修复前返回即崩(SIGSEGV) */
  MT_CHECK_EQ_U64(n, 1);
  MT_CHECK_EQ_U64(g_hook_calls, 1);
  const mosaic_function_record *r0 = mosaic_runtime_find_function(rt, 70ull << 32);
  MT_CHECK_EQ_U64(mf_flags(r0) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);   /* 已墓碑 */

  /* dispatch 末尾已 flush pending dlclose → 模块 .so 已卸载;同模块另一函数
     重新物化走完整 dlopen 路径 → 成功(验证延迟卸载后模块可再次加载) */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (70ull << 32) | 1);
  MT_CHECK(f1 != NULL);

  munmap(fence, (size_t)pg);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_dispatch_executes_subscribers);
  MT_RUN(test_dispatch_tombstone_restore_cycle);
  MT_RUN(test_dispatch_self_tombstone_reentrancy);
  MT_RUN(test_self_tombstone_single_ref);
  return MT_RESULT() ? 0 : 1;
}
