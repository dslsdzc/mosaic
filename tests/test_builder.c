#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_PATH "/tmp/mosaic_test_builder.pack"

static int build_pack(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(PACK_PATH, 2, 4, 3, 1, 2);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "player_join");   /* id 0 */
  mosaic_pack_builder_add_event(b, "block_break");   /* id 1 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", "/tmp/a.so");
  mosaic_pack_builder_add_module(b, 20, 2, "mod_b", "/tmp/b.so");
  /* fn id = module<<32|local */
  mosaic_pack_builder_add_fn(b, 20, 0, 1, 64, 1, 3, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 1, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 1, 0, 0, 1, 1, 0);
  mosaic_pack_builder_add_fn(b, 20, 1, 2, 64, 1, 2, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 1, 20ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 1);
  mosaic_pack_builder_add_dep(b, 20, 10);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_built_pack(void) {
  MT_CHECK(build_pack() == 0);
  FILE *f = fopen(PACK_PATH, "rb");
  MT_CHECK(f != NULL);
  u8 hdr[HDR_SIZE]; MT_CHECK(fread(hdr, 1, HDR_SIZE, f) == HDR_SIZE);
  MT_CHECK_EQ_U64(hdr_magic(hdr), MOSAIC_PACK_MAGIC);
  MT_CHECK_EQ_U64(hdr_version(hdr), MOSAIC_PACK_VERSION);
  MT_CHECK_EQ_U64(hdr_module_count(hdr), 2);
  MT_CHECK_EQ_U64(hdr_fn_count(hdr), 4);
  MT_CHECK_EQ_U64(hdr_trigger_count(hdr), 3);
  MT_CHECK_EQ_U64(hdr_dep_count(hdr), 1);
  MT_CHECK_EQ_U64(hdr_event_count(hdr), 2);

  fseek(f, (long)hdr_module_off(hdr), SEEK_SET);
  mosaic_module_record mods[2];
  MT_CHECK(fread(mods, 1, sizeof mods, f) == sizeof mods);
  /* 模块按 id 排序:10 在前,20 在后 */
  MT_CHECK_EQ_U64(mm_id(&mods[0]), 10); MT_CHECK_EQ_U64(mm_id(&mods[1]), 20);
  MT_CHECK_EQ_U64(mm_fn_base(&mods[0]), 0); MT_CHECK_EQ_U64(mm_fn_count(&mods[0]), 2);
  MT_CHECK_EQ_U64(mm_fn_base(&mods[1]), 2); MT_CHECK_EQ_U64(mm_fn_count(&mods[1]), 2);
  MT_CHECK_EQ_U64(mm_dep_off(&mods[0]), MOSAIC_DEP_NONE);   /* 无依赖 */
  MT_CHECK_EQ_U64(mm_dep_off(&mods[1]), 0);                 /* 依赖表第 0 项 */
  MT_CHECK(mm_name_off(&mods[0]) != 0);

  fseek(f, (long)hdr_fn_off(hdr), SEEK_SET);
  mosaic_function_record fns[4];
  MT_CHECK(fread(fns, 1, sizeof fns, f) == sizeof fns);
  /* 按 (module,local) 排序:10|0, 10|1, 20|0, 20|1 */
  MT_CHECK_EQ_U64(mf_id(&fns[0]), 10ull << 32 | 0);
  MT_CHECK_EQ_U64(mf_id(&fns[1]), 10ull << 32 | 1);
  MT_CHECK_EQ_U64(mf_id(&fns[2]), 20ull << 32 | 0);
  MT_CHECK_EQ_U64(mf_id(&fns[3]), 20ull << 32 | 1);
  MT_CHECK_EQ_U64(mf_code_off(&fns[0]), 0);
  MT_CHECK_EQ_U64(mf_code_off(&fns[3]), 2);
  MT_CHECK_EQ_U64(mf_state_size(&fns[1]), 0);
  MT_CHECK_EQ_U64(mf_flags(&fns[0]) & MOSAIC_FN_REQUIRES_STATE, MOSAIC_FN_REQUIRES_STATE);
  MT_CHECK_EQ_U64(mf_flags(&fns[1]) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);

  fseek(f, (long)hdr_trigger_off(hdr), SEEK_SET);
  mosaic_trigger_entry trigs[3];
  MT_CHECK(fread(trigs, 1, sizeof trigs, f) == sizeof trigs);
  /* 按 (event,fn) 排序:event0 两条在前,event1 一条在后 */
  MT_CHECK_EQ_U64(mt_event_id(&trigs[0]), 0);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[1]), 0);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[2]), 1);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[0]), 10ull << 32 | 0);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[1]), 10ull << 32 | 1);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[2]), 20ull << 32 | 0);

  fclose(f);
}

static void test_duplicate_fn_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_dup.pack", 1, 2, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "m", "/tmp/x.so");
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 0, 1, 0, 0);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 0, 1, 0, 0);  /* 重复 local_id */
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "duplicate function id") != NULL);
  mosaic_pack_builder_free(b);
}

int main(void) {
  MT_RUN(test_built_pack);
  MT_RUN(test_duplicate_fn_rejected);
  return MT_RESULT() ? 0 : 1;
}
