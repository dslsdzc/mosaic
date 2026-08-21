#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic/ownership.h"
#include "mosaic/eviction.h"
#include "mosaic_internal.h"   /* 偏差 D-9-1:ws_find 断言需要内部头(与 test_lifecycle D-3 同模式) */
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;

static int build_pack(const char *path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 2, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "mod", SO_PATH);
  for (int i = 0; i < 2; i++)
    mosaic_pack_builder_add_fn(b, 10, (u64)i, 0, 64, 1, 0,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_lease_blocks_tombstone(void) {
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_own.pack") == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_own.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  const u64 F0 = 10ull << 32;
  mosaic_lease *l = mosaic_lease_acquire(rt, F0);
  MT_CHECK(l != NULL);
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  MT_CHECK_EQ_U64(fn->refs, 1);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_BUSY);
  /* 驱逐也不能动它 */
  mosaic_evict_config cfg = { 0 };
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &cfg), 0);
  mosaic_lease_release(l);
  MT_CHECK_EQ_U64(fn->refs, 0);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == 0);
  mosaic_runtime_close(rt);
}

static void test_evict_window(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_own.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (10ull << 32) | 1);
  MT_CHECK(f0 != NULL && f1 != NULL);
  /* 大窗口:无人过期 */
  mosaic_evict_config big = { 1000000000000ull };   /* 1000s */
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &big), 0);
  MT_CHECK(ws_find(rt, 10ull << 32) != NULL);
  /* 零窗口:全部过期 → 墓碑 2 个 */
  mosaic_evict_config zero = { 0 };
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &zero), 2);
  MT_CHECK(ws_find(rt, 10ull << 32) == NULL);
  MT_CHECK(ws_find(rt, (10ull << 32) | 1) == NULL);
  /* 墓碑后可恢复 */
  mosaic_fn_obj *f0b = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0b != NULL);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_lease_blocks_tombstone);
  MT_RUN(test_evict_window);
  return MT_RESULT() ? 0 : 1;
}
