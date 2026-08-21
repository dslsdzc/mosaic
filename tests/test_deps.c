/* tests/test_deps.c — M2-1:依赖图遍历 + 闭包解析
 *
 * 单 pack(CHAIN_PATH)场景:模块 100→200→300 链式依赖;400 无依赖;
 * 500→999 缺失依赖;600↔700 环;800→800 自环。全部无函数、无触发器
 * (依赖解析只读记录,永不 dlopen,so_path 填假路径即可)。
 * 跨 pack 场景:P0 模块 10(依赖 30)/20,P1 模块 30(open_many 合并视图)。 */
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/deps.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHAIN_PATH "/tmp/mosaic_deps_chain.pack"
#define P0_PATH    "/tmp/mosaic_deps_p0.pack"
#define P1_PATH    "/tmp/mosaic_deps_p1.pack"

/* 依赖 100→200→300(链),400 无依赖,500→999(缺失),600↔700(环),800→800(自环) */
static int build_chain(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(CHAIN_PATH, 8, 0, 0, 6, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 100, 1, "mod_a", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 200, 2, "mod_b", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 300, 3, "mod_c", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 400, 4, "mod_d", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 500, 1, "mod_e", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 600, 1, "mod_f", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 700, 1, "mod_g", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 800, 1, "mod_h", "/nonexistent.so");
  mosaic_pack_builder_add_dep(b, 100, 200);
  mosaic_pack_builder_add_dep(b, 200, 300);
  mosaic_pack_builder_add_dep(b, 500, 999);
  mosaic_pack_builder_add_dep(b, 600, 700);
  mosaic_pack_builder_add_dep(b, 700, 600);
  mosaic_pack_builder_add_dep(b, 800, 800);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build chain: %s\n", err);
  return rc;
}

/* P0:模块 10(依赖 30,跨 pack)、20;P1:模块 30。事件集相同 → open_many 接受 */
static int build_p0(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(P0_PATH, 2, 0, 0, 1, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 10, 1, "mod_10", "/nonexistent.so");
  mosaic_pack_builder_add_module(b, 20, 1, "mod_20", "/nonexistent.so");
  mosaic_pack_builder_add_dep(b, 10, 30);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build p0: %s\n", err);
  return rc;
}

static int build_p1(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(P1_PATH, 1, 0, 0, 0, 2);
  mosaic_pack_builder_add_event(b, "player_join");
  mosaic_pack_builder_add_event(b, "block_break");
  mosaic_pack_builder_add_module(b, 30, 1, "mod_30", "/nonexistent.so");
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build p1: %s\n", err);
  return rc;
}

/* ---- 断言辅助 ---- */
static size_t index_of(const u64 *arr, size_t len, u64 id) {
  for (size_t i = 0; i < len; i++) if (arr[i] == id) return i;
  return (size_t)-1;
}
static int contains(const u64 *arr, size_t len, u64 id) { return index_of(arr, len, id) != (size_t)-1; }
/* 依赖先于依赖者 */
static int dep_before(const u64 *out, size_t len, u64 dep, u64 owner) {
  size_t d = index_of(out, len, dep), o = index_of(out, len, owner);
  return d != (size_t)-1 && o != (size_t)-1 && d < o;
}

struct collect_ctx { u64 *arr; size_t len; };
static int collect_cb(u64 dep, void *user) {
  struct collect_ctx *c = user;
  if (c->len < 16) c->arr[c->len++] = dep;
  return 0;
}
static int first_cb(u64 dep, void *user) { *(u64 *)user = dep; return 1; }   /* 首条即停 */

/* ---- 用例 ---- */
static void test_for_each_dep(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 got[16];
  struct collect_ctx c = { got, 0 };
  MT_CHECK(mosaic_module_for_each_dep(rt, 100, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 1); MT_CHECK_EQ_U64(got[0], 200);
  c.len = 0;
  MT_CHECK(mosaic_module_for_each_dep(rt, 200, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 1); MT_CHECK_EQ_U64(got[0], 300);
  c.len = 0;
  MT_CHECK(mosaic_module_for_each_dep(rt, 300, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 0);                    /* 无依赖 → 回调零次 */
  c.len = 0;
  MT_CHECK(mosaic_module_for_each_dep(rt, 400, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 0);
  /* 回调返回非 0 → 停止并透传;首条即停 → 只见 200 */
  u64 first = 0;
  MT_CHECK(mosaic_module_for_each_dep(rt, 100, first_cb, &first) == 1);
  MT_CHECK_EQ_U64(first, 200);
  mosaic_runtime_close(rt);
}

static void test_for_each_dep_missing(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 got[16];
  struct collect_ctx c = { got, 0 };
  MT_CHECK(mosaic_module_for_each_dep(rt, 4242, collect_cb, &c) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  MT_CHECK_EQ_U64(c.len, 0);
  mosaic_runtime_close(rt);
}

static void test_resolve_chain(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 out[8];
  size_t n = 0;
  int rc = mosaic_dep_resolve(rt, 100, NULL, out, 8, &n);
  MT_CHECK(rc == 0);
  MT_CHECK_EQ_U64(n, 3);
  MT_CHECK(contains(out, n, 100) && contains(out, n, 200) && contains(out, n, 300));
  MT_CHECK(dep_before(out, n, 200, 100));   /* 依赖先于依赖者 */
  MT_CHECK(dep_before(out, n, 300, 200));
  MT_CHECK(!contains(out, n, 400));         /* 闭包不含无关模块 */
  mosaic_runtime_close(rt);
}

static void test_resolve_no_deps(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 out[8];
  size_t n = 0;
  /* 无依赖模块 → 闭包 = 自身 */
  MT_CHECK(mosaic_dep_resolve(rt, 400, NULL, out, 8, &n) == 0);
  MT_CHECK_EQ_U64(n, 1); MT_CHECK_EQ_U64(out[0], 400);
  MT_CHECK(mosaic_dep_resolve(rt, 300, NULL, out, 8, &n) == 0);
  MT_CHECK_EQ_U64(n, 1); MT_CHECK_EQ_U64(out[0], 300);   /* 叶子也无依赖 */
  /* 入口模块不存在 → NOT_FOUND(存在性检查先于一切) */
  MT_CHECK(mosaic_dep_resolve(rt, 4242, NULL, out, 8, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

static void test_resolve_missing_dep(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 out[8];
  size_t n = 0;
  MT_CHECK(mosaic_dep_resolve(rt, 500, NULL, out, 8, &n) == -1);   /* 500→999 缺失 */
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

static void test_resolve_cycle(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 out[8];
  size_t n = 0;
  /* 600↔700 互环 */
  MT_CHECK(mosaic_dep_resolve(rt, 600, NULL, out, 8, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  MT_CHECK(mosaic_dep_resolve(rt, 700, NULL, out, 8, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  /* 800→800 自环 */
  MT_CHECK(mosaic_dep_resolve(rt, 800, NULL, out, 8, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  mosaic_runtime_close(rt);
}

static void test_resolve_probe(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  size_t n = 0;
  /* 探测模式:out_cap=0 → 只写所需长度,返回 0 */
  MT_CHECK(mosaic_dep_resolve(rt, 100, NULL, NULL, 0, &n) == 0);
  MT_CHECK_EQ_U64(n, 3);
  /* 两阶段:探测 → 填充 */
  u64 out[3];
  MT_CHECK(mosaic_dep_resolve(rt, 100, NULL, out, 3, &n) == 0);
  MT_CHECK_EQ_U64(n, 3);
  MT_CHECK(dep_before(out, n, 200, 100) && dep_before(out, n, 300, 200));
  /* 容量不足(>0 但 < 所需)→ -1 + NOMEM,out_len 仍给所需长度 */
  u64 small[2];
  n = 0;
  MT_CHECK(mosaic_dep_resolve(rt, 100, NULL, small, 2, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOMEM);
  MT_CHECK_EQ_U64(n, 3);
  mosaic_runtime_close(rt);
}

static void test_resolve_self_constraint(void) {
  char err[256];
  MT_CHECK(build_chain() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(CHAIN_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u64 out[8];
  size_t n = 0;
  /* 模块 100 版本 = 1 */
  MT_CHECK(mosaic_dep_resolve(rt, 100, NULL, out, 8, &n) == 0);                       /* NULL = 无约束 */
  MT_CHECK(mosaic_dep_resolve(rt, 100, &(mosaic_version_constraint){0, 0}, out, 8, &n) == 0);   /* 显式无界 */
  MT_CHECK(mosaic_dep_resolve(rt, 100, &(mosaic_version_constraint){1, 1}, out, 8, &n) == 0);
  MT_CHECK(mosaic_dep_resolve(rt, 100, &(mosaic_version_constraint){0, 3}, out, 8, &n) == 0);
  /* 违反 self_constraint → ABI(先于 DFS,闭包不输出) */
  MT_CHECK(mosaic_dep_resolve(rt, 100, &(mosaic_version_constraint){2, 0}, out, 8, &n) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ABI);
  MT_CHECK_EQ_U64(n, 0);
  mosaic_runtime_close(rt);
}

static void test_resolve_cross_pack(void) {
  char err[256];
  MT_CHECK(build_p0() == 0 && build_p1() == 0);
  const char *paths[2] = { P0_PATH, P1_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 2, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) { fprintf(stderr, "open_many: %s\n", err); return; }
  u64 out[8];
  size_t n = 0;
  /* A(pack0) 依赖 B(pack1):闭包 = {30, 10},拓扑序 B 先于 A */
  MT_CHECK(mosaic_dep_resolve(rt, 10, NULL, out, 8, &n) == 0);
  MT_CHECK_EQ_U64(n, 2);
  MT_CHECK(contains(out, n, 10) && contains(out, n, 30));
  MT_CHECK(dep_before(out, n, 30, 10));
  /* 跨 pack 依赖的目标自身:闭包 = 自身 */
  MT_CHECK(mosaic_dep_resolve(rt, 30, NULL, out, 8, &n) == 0);
  MT_CHECK_EQ_U64(n, 1); MT_CHECK_EQ_U64(out[0], 30);
  /* pack0 内无依赖模块:闭包 = 自身 */
  MT_CHECK(mosaic_dep_resolve(rt, 20, NULL, out, 8, &n) == 0);
  MT_CHECK_EQ_U64(n, 1); MT_CHECK_EQ_U64(out[0], 20);
  /* 跨 pack 遍历:for_each(10) 命中 pack1 的 30 */
  u64 got[16];
  struct collect_ctx c = { got, 0 };
  MT_CHECK(mosaic_module_for_each_dep(rt, 10, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 1); MT_CHECK_EQ_U64(got[0], 30);
  mosaic_runtime_close(rt);
}

int main(void) {
  MT_RUN(test_for_each_dep);
  MT_RUN(test_for_each_dep_missing);
  MT_RUN(test_resolve_chain);
  MT_RUN(test_resolve_no_deps);
  MT_RUN(test_resolve_missing_dep);
  MT_RUN(test_resolve_cycle);
  MT_RUN(test_resolve_probe);
  MT_RUN(test_resolve_self_constraint);
  MT_RUN(test_resolve_cross_pack);
  return MT_RESULT() ? 0 : 1;
}
