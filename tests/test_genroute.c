/* tests/test_genroute.c — M2-2a:generation 路由表 + builder 变换索引设置器
 *
 * 覆盖:put/get 往返(含覆盖与未命中 0)、扩容(1000 条)、swap 语义(新表
 * 生效、旧表指针返回、free 旧表后新表独立、NULL slot 首次 swap)、键位混合
 * 分布(module<<32|local 模式下 1000 条负载 <70% 无长簇)、builder 设置器
 * 往返(finish 写出 reserved,运行时读回)、runtime_close 释放路由表。 */
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic_internal.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSFORM_PACK "/tmp/mosaic_genroute_transform.pack"

/* 与 src/genroute.c 同纪律的键位混合(测试侧复算,验证分布断言) */
static u64 key_mix(u64 fn_id) {
  return (fn_id >> 32) ^ (fn_id * 0x9E3779B97F4A7C15ull);
}

static void test_put_get_roundtrip(void) {
  struct gen_route t = { 0 };
  u64 id = (3ull << 32) | 7;
  MT_CHECK(gen_route_put(&t, id, 2) == 0);
  MT_CHECK_EQ_U64(gen_route_get(&t, id), 2);
  MT_CHECK_EQ_U64(t.len, 1);
  /* 覆盖已有条目(commit 幂等) */
  MT_CHECK(gen_route_put(&t, id, 5) == 0);
  MT_CHECK_EQ_U64(gen_route_get(&t, id), 5);
  MT_CHECK_EQ_U64(t.len, 1);
  /* 未命中 → 0(回落基础 pack 原始记录) */
  MT_CHECK_EQ_U64(gen_route_get(&t, (3ull << 32) | 8), 0);
  MT_CHECK_EQ_U64(gen_route_get(&t, (4ull << 32) | 7), 0);
  /* gen==0 与"无条目"哨兵冲突,拒绝 */
  MT_CHECK(gen_route_put(&t, (9ull << 32) | 1, 0) == -1);
  MT_CHECK_EQ_U64(gen_route_get(&t, (9ull << 32) | 1), 0);
  gen_route_free(&t);
  /* 释放后表清零可复用 */
  MT_CHECK_EQ_U64(t.cap, 0);
  MT_CHECK(t.keys == NULL && t.gens == NULL);
}

static void test_growth(void) {
  struct gen_route t = { 0 };
  for (u64 i = 1; i <= 1000; i++) {
    u64 fn_id = (7ull << 32) | i;
    MT_CHECK(gen_route_put(&t, fn_id, (u32)(i % 3) + 1) == 0);
  }
  MT_CHECK_EQ_U64(t.len, 1000);
  MT_CHECK(t.cap >= 1000);            /* 70% 负载扩容 ×2 后容量 ≥ 条目数 */
  for (u64 i = 1; i <= 1000; i++)
    MT_CHECK_EQ_U64(gen_route_get(&t, (7ull << 32) | i), (u32)(i % 3) + 1);
  MT_CHECK_EQ_U64(gen_route_get(&t, (7ull << 32) | 1001), 0);   /* 界外未命中 */
  gen_route_free(&t);
}

static void test_swap(void) {
  /* 堆表(runtime 用法):a = 旧表(commit 前的路由),b = 新表 */
  struct gen_route *a = calloc(1, sizeof *a);
  struct gen_route *b = calloc(1, sizeof *b);
  MT_CHECK(a && b);
  if (!a || !b) { free(a); free(b); return; }
  MT_CHECK(gen_route_put(a, (1ull << 32) | 1, 2) == 0);
  MT_CHECK(gen_route_put(a, (1ull << 32) | 3, 2) == 0);
  MT_CHECK(gen_route_put(b, (1ull << 32) | 1, 5) == 0);
  MT_CHECK(gen_route_put(b, (1ull << 32) | 2, 5) == 0);
  /* swap:新表生效、旧表指针返回、旧表条目不在新表 */
  struct gen_route *cur = a;
  struct gen_route *old = gen_route_swap(&cur, b);
  MT_CHECK(old == a);
  MT_CHECK(cur == b);
  MT_CHECK_EQ_U64(gen_route_get(cur, (1ull << 32) | 1), 5);
  MT_CHECK_EQ_U64(gen_route_get(cur, (1ull << 32) | 2), 5);
  MT_CHECK_EQ_U64(gen_route_get(cur, (1ull << 32) | 3), 0);
  /* free 旧表后新表独立(数组内存不共享) */
  gen_route_free(old); free(old);
  MT_CHECK_EQ_U64(gen_route_get(cur, (1ull << 32) | 1), 5);
  MT_CHECK_EQ_U64(gen_route_get(cur, (1ull << 32) | 2), 5);
  gen_route_free(cur); free(cur);
  /* NULL slot 首次 swap(commit 尚无路由时) */
  struct gen_route *first = calloc(1, sizeof *first);
  struct gen_route *slot = NULL;
  MT_CHECK(gen_route_put(first, (2ull << 32) | 9, 3) == 0);
  MT_CHECK(gen_route_swap(&slot, first) == NULL);
  MT_CHECK(slot == first);
  MT_CHECK_EQ_U64(gen_route_get(slot, (2ull << 32) | 9), 3);
  gen_route_free(first); free(first);
}

/* 键位混合分布:fn_id 模式 (module<<32)|local、module 连续 → 直接 & 掩码会
   塌缩到少量槽(ws_hash 历史 bug 的同款退化:40 槽/25 簇);混合后 1000 条
   (25 模块 × 40 局部,负载 ≈49%,cap 2048)最大探测长度应远小于表容量
   (宽松界 ≤8;实测 1——异或模块高半把同 local 的跨模块键分开)。
   注:小到病态的 (module, local) 区间组合(如 40×25)与 11 位掩码的算术
   结构仍可能聚簇——那是所有开放寻址哈希对极小键空间的共性,不是混合
   纪律缺陷;1e5 规模实测 max_probe=1。 */
static void test_hash_distribution(void) {
  struct gen_route t = { 0 };
  for (u64 m = 1; m <= 25; m++)
    for (u64 l = 1; l <= 40; l++)
      MT_CHECK(gen_route_put(&t, (m << 32) | l, 2) == 0);
  MT_CHECK_EQ_U64(t.len, 1000);
  MT_CHECK(t.cap >= 2048);            /* 1000 条 → 末次扩容后 ≥ 2048 */
  u64 max_probe = 0;
  u64 mask = t.cap - 1;
  for (u64 i = 0; i < t.cap; i++) {
    if (!t.keys[i]) continue;
    u64 probe = 1;                    /* 首槽命中 = 1 次探测 */
    u64 j = key_mix(t.keys[i]) & mask;
    while (j != i) { j = (j + 1) & mask; probe++; }
    if (probe > max_probe) max_probe = probe;
  }
  MT_CHECK(max_probe <= 8);
  gen_route_free(&t);
}

/* builder 设置器往返:set_fn_transform 写 reserved;finish 随记录写出;
   运行时 find_function 读回;未知 fn_id → -1 且不使构建失败 */
static void test_builder_transform_setter(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(TRANSFORM_PACK, 1, 2, 0, 0, 1);
  MT_CHECK(b != NULL);
  if (!b) return;
  mosaic_pack_builder_add_event(b, "e");
  mosaic_pack_builder_add_module(b, 1, 1, "mod", "/nonexistent.so");
  mosaic_pack_builder_add_fn(b, 1, 10, 0, 64, 1, 1, 0);
  mosaic_pack_builder_add_fn(b, 1, 11, 1, 64, 1, 1, 0);
  MT_CHECK(mosaic_pack_builder_set_fn_transform(b, (1ull << 32) | 11, 3) == 0);
  MT_CHECK(mosaic_pack_builder_set_fn_transform(b, (1ull << 32) | 10, 0) == 0);   /* 0 = 无变换 */
  MT_CHECK(mosaic_pack_builder_set_fn_transform(b, 999ull, 2) == -1);              /* 未知 fn_id */
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == 0);
  mosaic_pack_builder_free(b);

  mosaic_runtime *rt = mosaic_runtime_open(TRANSFORM_PACK, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  const mosaic_function_record *r10 = mosaic_runtime_find_function(rt, (1ull << 32) | 10);
  const mosaic_function_record *r11 = mosaic_runtime_find_function(rt, (1ull << 32) | 11);
  MT_CHECK(r10 != NULL && r11 != NULL);
  if (!r10 || !r11) { mosaic_runtime_close(rt); return; }
  MT_CHECK_EQ_U64(mf_reserved(r10), 0);
  MT_CHECK_EQ_U64(mf_reserved(r11), 3);   /* transform_index=3 → abi->transforms[2],M2-2b 消费 */
  mosaic_runtime_close(rt);
}

/* runtime 集成:routes 挂到 rt 后 close 释放(路径:gen_route_free 数组 +
   free 表体;ASan/Valgrind 下可证,无泄漏即通过) */
static void test_runtime_close_with_routes(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(TRANSFORM_PACK, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  struct gen_route *r = calloc(1, sizeof *r);
  MT_CHECK(r != NULL);
  if (!r) { mosaic_runtime_close(rt); return; }
  MT_CHECK(gen_route_put(r, (1ull << 32) | 10, 2) == 0);
  rt->routes = r;   /* 模拟 M2-2b commit 挂载 */
  MT_CHECK_EQ_U64(gen_route_get(rt->routes, (1ull << 32) | 10), 2);
  mosaic_runtime_close(rt);   /* 不得崩溃/泄漏 */
}

int main(void) {
  MT_RUN(test_put_get_roundtrip);
  MT_RUN(test_growth);
  MT_RUN(test_swap);
  MT_RUN(test_hash_distribution);
  MT_RUN(test_builder_transform_setter);
  MT_RUN(test_runtime_close_with_routes);
  return MT_RESULT() ? 0 : 1;
}
