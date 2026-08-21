#include "synth_universe.h"
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int mosaic_bench_build_universe(const char *pack_path, const char *so_path,
                                u64 n_fns, u64 n_modules, u32 n_events, u32 triggers_per_fn) {
  char err[256];
  u64 rng = 0x9E3779B97F4A7C15ull;       /* 种子 */
  const char *ev_names[] = { "player_join", "block_break", "item_use", "entity_spawn", "tick" };
  if (n_events > 5) n_events = 5;

  u64 n_triggers = n_fns * triggers_per_fn;
  u64 n_deps = n_modules > 1 ? n_modules - 1 : 0;
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, n_modules, n_fns, n_triggers, n_deps, n_events);
  if (!b) return -1;
  for (u32 i = 0; i < n_events; i++) mosaic_pack_builder_add_event(b, ev_names[i]);

  /* 模块 id 取连续值 1..n_modules(确定性,无碰撞——随机 id 在百万量级
     会撞车 ~39%,构建必然失败;稀疏随机 id 留给测试场景) */
  char name[64], so[256];
  u64 *mod_ids_arr = malloc(n_modules * sizeof(u64));
  for (u64 i = 0; i < n_modules; i++) {
    u64 mid = i + 1;
    mod_ids_arr[i] = mid;
    snprintf(name, sizeof name, "mod_%llu", (unsigned long long)mid);
    snprintf(so, sizeof so, "%s", so_path);
    mosaic_pack_builder_add_module(b, mid, 1, name, so);
  }
  u64 per_mod = n_fns / n_modules;
  u64 local = 0;
  for (u64 i = 0; i < n_fns; i++) {
    u64 mi = i / per_mod;
    u32 code = (u32)(mosaic_bench_rng(&rng) % 3);
    mosaic_pack_builder_add_fn(b, mod_ids_arr[mi], local++, code, 64, 1,
                               (u32)(mosaic_bench_rng(&rng) % 16),
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    if ((i + 1) % per_mod == 0) local = 0;
  }
  /* 触发:每函数订阅 triggers_per_fn 个不同事件 */
  for (u64 i = 0; i < n_fns; i++) {
    u64 mi = i / per_mod;
    u64 fn_id = (mod_ids_arr[mi] << 32) | (i % per_mod);
    for (u32 k = 0; k < triggers_per_fn; k++) {
      u32 e = (u32)(mosaic_bench_rng(&rng) % n_events);
      mosaic_pack_builder_add_trigger(b, e, fn_id);
    }
  }
  for (u64 i = 1; i < n_modules; i++)
    mosaic_pack_builder_add_dep(b, mod_ids_arr[i], mod_ids_arr[i - 1]);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "universe build: %s\n", err);
  mosaic_pack_builder_free(b);
  free(mod_ids_arr);
  return rc;
}

int mosaic_bench_build_solo(const char *pack_path, const char *so_path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, 1, 1, 1, 0, 1);
  mosaic_pack_builder_add_event(b, "solo");
  mosaic_pack_builder_add_module(b, 42, 1, "solo_mod", so_path);
  mosaic_pack_builder_add_fn(b, 42, 0, 0, 64, 1, 0,
                             MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 42ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "solo build: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

int mosaic_bench_build_cold(const char *pack_path, const char *so_path) {
  /* S2 专用:1 模块 1000 函数,1 事件 "cold",全部订阅 → 一次派发物化 1000 个 */
  char err[256];
  enum { COLD_FNS = 1000 };
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, 1, COLD_FNS, COLD_FNS, 0, 1);
  mosaic_pack_builder_add_event(b, "cold");
  mosaic_pack_builder_add_module(b, 7, 1, "cold_mod", so_path);
  for (u64 i = 0; i < COLD_FNS; i++) {
    mosaic_pack_builder_add_fn(b, 7, i, (u32)(i % 3), 64, 1, (u32)(i % 16),
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    mosaic_pack_builder_add_trigger(b, 0, (7ull << 32) | i);
  }
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "cold build: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

long mosaic_bench_rss_kb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return -1;
  char line[256]; long kb = -1;
  while (fgets(line, sizeof line, f))
    if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
  fclose(f);
  return kb;
}

double mosaic_bench_now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}
