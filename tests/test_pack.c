#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mini_test.h"
#include <string.h>

static void test_record_sizes(void) {
  MT_CHECK(sizeof(mosaic_function_record) == 48);
  MT_CHECK(sizeof(mosaic_module_record) == 64);
  MT_CHECK(sizeof(mosaic_trigger_entry) == 16);
  MT_CHECK(sizeof(mosaic_dep_entry) == 16);
  MT_CHECK(HDR_SIZE == 256);
}

static void test_fn_accessors(void) {
  mosaic_function_record r; memset(&r, 0, sizeof r);
  mf_set_id(&r, 0x1122334455667788ull);
  mf_set_module_id(&r, 42);
  mf_set_code_off(&r, 7);
  mf_set_state_off(&r, 4096);
  mf_set_flags(&r, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_STATE_ACTIVE);
  mf_set_generation(&r, 2);
  mf_set_state_size(&r, 64);
  mf_set_cost_hint(&r, 5);
  MT_CHECK_EQ_U64(mf_id(&r), 0x1122334455667788ull);
  MT_CHECK_EQ_U64(mf_module_id(&r), 42);
  MT_CHECK_EQ_U64(mf_code_off(&r), 7);
  MT_CHECK_EQ_U64(mf_state_off(&r), 4096);
  MT_CHECK_EQ_U64(mf_flags(&r) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_ACTIVE);
  MT_CHECK(mf_flags(&r) & MOSAIC_FN_REQUIRES_STATE);
  MT_CHECK_EQ_U64(mf_generation(&r), 2);
  MT_CHECK_EQ_U64(mf_state_size(&r), 64);
  MT_CHECK_EQ_U64(mf_cost_hint(&r), 5);
}

static void test_module_accessors(void) {
  mosaic_module_record m; memset(&m, 0, sizeof m);
  mm_set_id(&m, 9); mm_set_fn_base(&m, 100); mm_set_fn_count(&m, 50);
  mm_set_dep_off(&m, 3); mm_set_name_off(&m, 8); mm_set_so_off(&m, 20);
  mm_set_version(&m, 1); mm_set_generation(&m, 1);
  MT_CHECK_EQ_U64(mm_id(&m), 9); MT_CHECK_EQ_U64(mm_fn_base(&m), 100);
  MT_CHECK_EQ_U64(mm_fn_count(&m), 50); MT_CHECK_EQ_U64(mm_dep_off(&m), 3);
  MT_CHECK_EQ_U64(mm_name_off(&m), 8); MT_CHECK_EQ_U64(mm_so_off(&m), 20);
  MT_CHECK_EQ_U64(mm_version(&m), 1); MT_CHECK_EQ_U64(mm_generation(&m), 1);
}

static void test_trigger_dep_accessors(void) {
  mosaic_trigger_entry t; memset(&t, 0, sizeof t);
  mt_set_event(&t, 77); mt_set_fn(&t, 0xAABBCCDD00112233ull);
  MT_CHECK_EQ_U64(mt_event_id(&t), 77);
  MT_CHECK_EQ_U64(mt_fn_id(&t), 0xAABBCCDD00112233ull);
  mosaic_dep_entry d; memset(&d, 0, sizeof d);
  md_set_owner(&d, 1); md_set_dep(&d, 2);
  MT_CHECK_EQ_U64(md_owner_id(&d), 1); MT_CHECK_EQ_U64(md_dep_id(&d), 2);
}

static void test_bytes_written_le(void) {
  mosaic_function_record r; memset(&r, 0, sizeof r);
  mf_set_id(&r, 0x0102030405060708ull);
  /* 小端:低字节在前 */
  MT_CHECK(r.bytes[FN_OFF_ID + 0] == 0x08 && r.bytes[FN_OFF_ID + 7] == 0x01);
}

int main(void) {
  MT_RUN(test_record_sizes);
  MT_RUN(test_fn_accessors);
  MT_RUN(test_module_accessors);
  MT_RUN(test_trigger_dep_accessors);
  MT_RUN(test_bytes_written_le);
  return MT_RESULT() ? 0 : 1;
}
