#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;   /* argv[1]:test_mod.so fixture(dispatch 断言需要真实模块) */

static int build_mini(const char *path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 1, 1, 0, 1);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_module(b, 10, 1, "mod", "/tmp/nonexistent.so");
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_open_good_pack(void) {
  MT_CHECK(build_mini("/tmp/mosaic_test_good.pack") == 0);
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_good.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (rt) {
    MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), 1);
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "tick"), 0);
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "nope"), MOSAIC_U32_NONE);
    mosaic_runtime_close(rt);
  }
}

static void test_event_prefix_rejected(void) {
  /* 最终评审必修:event_id 解析必须要求 name 整串相等,前缀/子串不得误匹配 */
  MT_CHECK(build_mini("/tmp/mosaic_test_good.pack") == 0);
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_good.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (rt) {
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "tick"), 0);        /* 精确名仍命中 */
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_joinx"), MOSAIC_U32_NONE); /* 前缀误匹配必须拒绝 */
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_joi"), MOSAIC_U32_NONE);   /* 截断子串拒绝 */
    mosaic_runtime_close(rt);
  }
}

static int build_prefix_pair(const char *path) {
  /* 事件注册顺序:先 "player_join"(注册 id 0),后 "player"(注册 id 1)。
     排序后:名字序 "player" < "player_join" → player=0, player_join=1;
     触发器引用注册 id 1("player"),finish 时重映射为排序后 id 0。 */
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 1, 1, 0, 2);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "player_join");  /* 注册 id 0 → 排序后 id 1 */
  mosaic_pack_builder_add_event(b, "player");       /* 注册 id 1 → 排序后 id 0 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod", SO_PATH);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 1, 10ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_event_prefix_pair_tiebreak(void) {
  /* M-2:长度 tiebreak 分支——prefix-equal 但长度不同的名字必须分属不同条目,
     二分查找不得把 "player" 与 "player_join" 误判为同一条 */
  MT_CHECK(build_prefix_pair("/tmp/mosaic_test_ppair.pack") == 0);
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_ppair.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u32 id_player = mosaic_runtime_event_id(rt, "player");
  u32 id_join = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK(id_player != MOSAIC_U32_NONE);
  MT_CHECK(id_join != MOSAIC_U32_NONE);
  MT_CHECK(id_player != id_join);
  MT_CHECK_EQ_U64(id_player, 0);   /* 名字序:player < player_join */
  MT_CHECK_EQ_U64(id_join, 1);
  /* 长度 tiebreak:prefix-equal 但长度不同的串必须全部 NONE(不同条目,非误匹配) */
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_j"), MOSAIC_U32_NONE);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_jo"), MOSAIC_U32_NONE);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "playerx"), MOSAIC_U32_NONE);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_joinx"), MOSAIC_U32_NONE);
  /* 触发器经重映射落到排序后 id 0 = runtime_event_id 查表结果 → dispatch 命中执行 ≥1 */
  MT_CHECK(mosaic_event_dispatch(rt, id_player, NULL) >= 1);
  mosaic_runtime_close(rt);
}

static void test_open_bad_magic(void) {
  FILE *f = fopen("/tmp/mosaic_test_badmagic.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, 0xDEADBEEF);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badmagic.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "magic") != NULL);
}

static void test_open_bad_version(void) {
  FILE *f = fopen("/tmp/mosaic_test_badver.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, 999);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badver.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "version") != NULL);
}

static void test_open_bad_offset(void) {
  FILE *f = fopen("/tmp/mosaic_test_badoff.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, MOSAIC_PACK_VERSION);
  hdr_set_module_off(hdr, 1u << 40);   /* 越界 */
  hdr_set_module_count(hdr, 100);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badoff.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "bounds") != NULL);
}

static void test_open_bad_meta_offset(void) {
  FILE *f = fopen("/tmp/mosaic_test_badmeta.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, MOSAIC_PACK_VERSION);
  hdr_set_meta_off(hdr, 1ull << 50);   /* 越界 */
  hdr_set_meta_len(hdr, 100);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badmeta.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "bounds") != NULL);
}

static void test_open_too_small(void) {
  FILE *f = fopen("/tmp/mosaic_test_tiny.pack", "wb");
  u8 buf[100]; memset(buf, 0x5A, sizeof buf);   /* 100B < HDR_SIZE 路径 */
  fwrite(buf, 1, sizeof buf, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_tiny.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "too small") != NULL);
}

static void test_open_count_overflow(void) {
  FILE *f = fopen("/tmp/mosaic_test_wrap.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, MOSAIC_PACK_VERSION);
  /* moff=0:旧校验 moff + (1ull<<63)*64 回绕成 0+32 可绕过;除法校验必须拒绝 */
  hdr_set_module_off(hdr, 0);
  hdr_set_module_count(hdr, 1ull << 63);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_wrap.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "bounds") != NULL);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_open_good_pack);
  MT_RUN(test_event_prefix_rejected);
  MT_RUN(test_event_prefix_pair_tiebreak);
  MT_RUN(test_open_bad_magic);
  MT_RUN(test_open_bad_version);
  MT_RUN(test_open_bad_offset);
  MT_RUN(test_open_bad_meta_offset);
  MT_RUN(test_open_too_small);
  MT_RUN(test_open_count_overflow);
  return MT_RESULT() ? 0 : 1;
}
