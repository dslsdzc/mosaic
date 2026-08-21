#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

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

int main(void) {
  MT_RUN(test_open_good_pack);
  MT_RUN(test_open_bad_magic);
  MT_RUN(test_open_bad_version);
  MT_RUN(test_open_bad_offset);
  return MT_RESULT() ? 0 : 1;
}
