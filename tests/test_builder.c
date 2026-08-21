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
  /* 触发器 event_id 已由 builder 从注册顺序重映射为排序位置:
     "block_break"(注册 1 → 排序 0)一条在前,"player_join"(注册 0 → 排序 1)两条在后,
     再按 (event,fn) 排序 */
  MT_CHECK_EQ_U64(mt_event_id(&trigs[0]), 0);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[1]), 1);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[2]), 1);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[0]), 20ull << 32 | 0);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[1]), 10ull << 32 | 0);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[2]), 10ull << 32 | 1);

  /* 事件名表按名排序落盘:"block_break" 在前,"player_join" 在后(注册顺序相反);
     偏移互异,且 meta blob 中对应偏移的字符串与名字一致 */
  fseek(f, (long)hdr_event_names_off(hdr), SEEK_SET);
  mosaic_event_name evs[2];
  MT_CHECK(fread(evs, 1, sizeof evs, f) == sizeof evs);
  MT_CHECK(mn_len(&evs[0]) != 0);
  MT_CHECK(mn_len(&evs[1]) != 0);
  MT_CHECK(mn_off(&evs[0]) != mn_off(&evs[1]));
  MT_CHECK_EQ_U64(mn_len(&evs[0]), (u64)strlen("block_break"));
  MT_CHECK_EQ_U64(mn_len(&evs[1]), (u64)strlen("player_join"));

  fseek(f, (long)hdr_meta_off(hdr), SEEK_SET);
  u8 meta[1024];
  MT_CHECK(fread(meta, 1, (size_t)hdr_meta_len(hdr), f) == hdr_meta_len(hdr));
  MT_CHECK(strcmp((const char *)meta + mn_off(&evs[0]), "block_break") == 0);
  MT_CHECK(strcmp((const char *)meta + mn_off(&evs[1]), "player_join") == 0);

  fclose(f);
}

static void test_multi_owner_deps(void) {
  char err[256];
  const char *p = "/tmp/mosaic_test_deps.pack";
  mosaic_pack_builder *b = mosaic_pack_builder_create(p, 2, 0, 0, 2, 0);
  if (!b) { MT_CHECK(0); return; }
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", "/tmp/a.so");
  mosaic_pack_builder_add_module(b, 20, 2, "mod_b", "/tmp/b.so");
  mosaic_pack_builder_add_dep(b, 10, 5);
  mosaic_pack_builder_add_dep(b, 20, 6);
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == 0);
  mosaic_pack_builder_free(b);

  FILE *f = fopen(p, "rb");
  MT_CHECK(f != NULL);
  u8 hdr[HDR_SIZE]; MT_CHECK(fread(hdr, 1, HDR_SIZE, f) == HDR_SIZE);
  fseek(f, (long)hdr_module_off(hdr), SEEK_SET);
  mosaic_module_record mods[2];
  MT_CHECK(fread(mods, 1, sizeof mods, f) == sizeof mods);
  MT_CHECK_EQ_U64(mm_id(&mods[0]), 10);
  MT_CHECK_EQ_U64(mm_id(&mods[1]), 20);
  /* 两个 owner 都必须拿到 dep_off,且指向各自依赖条目的下标 */
  MT_CHECK(mm_dep_off(&mods[0]) != MOSAIC_DEP_NONE);
  MT_CHECK(mm_dep_off(&mods[1]) != MOSAIC_DEP_NONE);
  MT_CHECK_EQ_U64(mm_dep_off(&mods[0]), 0);
  MT_CHECK_EQ_U64(mm_dep_off(&mods[1]), 1);

  fseek(f, (long)hdr_dep_off(hdr), SEEK_SET);
  mosaic_dep_entry deps[2];
  MT_CHECK(fread(deps, 1, sizeof deps, f) == sizeof deps);
  MT_CHECK_EQ_U64(md_owner_id(&deps[0]), 10); MT_CHECK_EQ_U64(md_dep_id(&deps[0]), 5);
  MT_CHECK_EQ_U64(md_owner_id(&deps[1]), 20); MT_CHECK_EQ_U64(md_dep_id(&deps[1]), 6);
  fclose(f);
}

static void test_duplicate_module_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_dupm.pack", 2, 0, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "a", "/tmp/a.so");
  mosaic_pack_builder_add_module(b, 10, 2, "b", "/tmp/b.so");  /* 重复 id */
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "duplicate module id") != NULL);
  mosaic_pack_builder_free(b);
}

static void test_duplicate_event_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_dupev.pack", 1, 0, 0, 0, 2);
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_event(b, "tick");  /* 重名:二分要求名字唯一 */
  mosaic_pack_builder_add_module(b, 10, 1, "a", "/tmp/a.so");
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "duplicate event name") != NULL);
  mosaic_pack_builder_free(b);
}

static void test_trigger_unknown_event_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_evref.pack", 1, 0, 1, 0, 1);
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_module(b, 10, 1, "a", "/tmp/a.so");
  mosaic_pack_builder_add_trigger(b, 5, 10ull << 32 | 0);  /* 引用不存在的注册 id */
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "trigger references unknown event") != NULL);
  mosaic_pack_builder_free(b);
}

static void test_too_many_events_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_ev65.pack", 1, 0, 0, 0, 65);
  mosaic_pack_builder_add_module(b, 10, 1, "a", "/tmp/a.so");
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "too many events") != NULL);
  mosaic_pack_builder_free(b);
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
  MT_RUN(test_multi_owner_deps);
  MT_RUN(test_duplicate_module_rejected);
  MT_RUN(test_duplicate_event_rejected);
  MT_RUN(test_trigger_unknown_event_rejected);
  MT_RUN(test_too_many_events_rejected);
  return MT_RESULT() ? 0 : 1;
}
