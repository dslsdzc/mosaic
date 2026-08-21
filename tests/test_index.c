#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic_internal.h"   /* 测试直接读 rt->map 校验布局 */
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_PATH "/tmp/mosaic_test_index.pack"

static u64 rng_state = 0x12345678ull;
static u64 rng_next(void) { /* xorshift64 */
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return rng_state;
}

#define N_MODULES 50
#define N_FNS 2000
#define N_EVENTS 4
#define N_TRIGGERS 1500

static u64 mod_ids[N_MODULES];
static u64 fn_ids[N_FNS];
static u32 fn_modules[N_FNS];
static u32 fn_codes[N_FNS];

static void build_random_universe(void) {
  char err[256];
  /* 模块 id 取稀疏随机值,验证排序查找 */
  for (int i = 0; i < N_MODULES; i++) mod_ids[i] = (u64)(rng_next() % 1000000) + 1;
  mosaic_pack_builder *b = mosaic_pack_builder_create(PACK_PATH, N_MODULES, N_FNS, N_TRIGGERS, N_MODULES - 1, N_EVENTS);
  const char *ev[N_EVENTS] = { "player_join", "block_break", "item_use", "entity_spawn" };
  for (int i = 0; i < N_EVENTS; i++) mosaic_pack_builder_add_event(b, ev[i]);
  char so[64], name[64];
  for (int i = 0; i < N_MODULES; i++) {
    snprintf(so, sizeof so, "/tmp/mod_%llu.so", (unsigned long long)mod_ids[i]);
    snprintf(name, sizeof name, "mod_%llu", (unsigned long long)mod_ids[i]);
    mosaic_pack_builder_add_module(b, mod_ids[i], 1, name, so);
  }
  int per_mod = N_FNS / N_MODULES;   /* 40 */
  for (int i = 0; i < N_FNS; i++) {
    int mi = i / per_mod;
    u64 local = (u64)(i % per_mod);
    u32 code = (u32)(rng_next() % 3);
    fn_ids[i] = (mod_ids[mi] << 32) | local;
    fn_modules[i] = (u32)mod_ids[mi];
    fn_codes[i] = code;
    mosaic_pack_builder_add_fn(b, mod_ids[mi], local, code, 64, 1, (u32)(rng_next() % 10), MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  }
  for (int i = 1; i < N_MODULES; i++) mosaic_pack_builder_add_dep(b, mod_ids[i], mod_ids[i - 1]);
  for (int i = 0; i < N_TRIGGERS; i++) {
    u32 e = (u32)(rng_next() % N_EVENTS);
    u64 fn = fn_ids[rng_next() % N_FNS];
    mosaic_pack_builder_add_trigger(b, e, fn);
  }
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) { fprintf(stderr, "finish: %s\n", err); exit(2); }
  mosaic_pack_builder_free(b);
}

static int naive_find_fn(u64 want, const mosaic_function_record **out, const u8 *map) {
  /* 朴素实现:全表线性扫描,对照物 */
  u64 n = hdr_fn_count(map);
  const mosaic_function_record *fns = (const mosaic_function_record *)(map + hdr_fn_off(map));
  for (u64 i = 0; i < n; i++)
    if (mf_id(&fns[i]) == want) { *out = &fns[i]; return 0; }
  return -1;
}

static void test_find_all_functions(void) {
  build_random_universe();
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), N_FNS);
  int hits = 0;
  for (int i = 0; i < N_FNS; i++) {
    const mosaic_function_record *r = mosaic_runtime_find_function(rt, fn_ids[i]);
    if (r) { hits++; MT_CHECK_EQ_U64(mf_id(r), fn_ids[i]); MT_CHECK_EQ_U64(mf_module_id(r), fn_modules[i]); }
    const mosaic_function_record *nr = NULL;
    int found = naive_find_fn(fn_ids[i], &nr, rt->map);
    MT_CHECK((r != NULL) == (found == 0));
  }
  MT_CHECK_EQ_U64(hits, N_FNS);
  /* 不存在的 id */
  MT_CHECK(mosaic_runtime_find_function(rt, 0) == NULL);
  MT_CHECK(mosaic_runtime_find_function(rt, 0xFFFFFFFFFFFFFFFFull) == NULL);
  mosaic_runtime_close(rt);
}

static void test_find_all_modules(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (int i = 0; i < N_MODULES; i++) {
    const mosaic_module_record *m = mosaic_runtime_find_module(rt, mod_ids[i]);
    MT_CHECK(m != NULL);
    if (m) {
      MT_CHECK_EQ_U64(mm_id(m), mod_ids[i]);
      MT_CHECK_EQ_U64(mm_fn_count(m), N_FNS / N_MODULES);
      const char *so = mosaic_runtime_module_string(rt, m, mm_so_off(m));
      MT_CHECK(so != NULL && strstr(so, "/tmp/mod_") != NULL);
      if (i > 0) MT_CHECK_EQ_U64(mm_dep_off(m) != MOSAIC_DEP_NONE, 1);
    }
  }
  MT_CHECK(mosaic_runtime_find_module(rt, 0) == NULL);
  mosaic_runtime_close(rt);
}

static void test_find_module_functions_are_contiguous(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (int i = 0; i < N_MODULES; i++) {
    const mosaic_module_record *m = mosaic_runtime_find_module(rt, mod_ids[i]);
    u32 base = mm_fn_base(m), cnt = mm_fn_count(m);
    const mosaic_function_record *fns = (const mosaic_function_record *)(rt->map + hdr_fn_off(rt->map));
    for (u32 j = 0; j < cnt; j++)
      MT_CHECK_EQ_U64(mf_module_id(&fns[base + j]), (u32)mod_ids[i]);
  }
  mosaic_runtime_close(rt);
}

static void test_event_lookup(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 事件 id = 排序位置:block_break=0, entity_spawn=1, item_use=2, player_join=3
     (注册顺序:player_join, block_break, item_use, entity_spawn) */
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_join"), 3);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "entity_spawn"), 1);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "item_use"), 2);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "block_break"), 0);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "unknown_event"), MOSAIC_U32_NONE);
  mosaic_runtime_close(rt);
}

int main(void) {
  MT_RUN(test_find_all_functions);
  MT_RUN(test_find_all_modules);
  MT_RUN(test_find_module_functions_are_contiguous);
  MT_RUN(test_event_lookup);
  return MT_RESULT() ? 0 : 1;
}
