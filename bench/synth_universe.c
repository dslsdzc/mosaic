#include "synth_universe.h"
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/resource.h>   /* M1.5-B:每片构建峰值(ru_maxrss 高水位) */
#include <sys/stat.h>       /* mkdir(shard_dir) */

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

/* ---- M1.5-B:分片宇宙构建器 ---- */
/* 进程高水位 (KB);-1 = 不可用 */
static long bench_maxrss_kb(void) {
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
  return ru.ru_maxrss;
}

/* 最近一次分片构建的单分片峰值(KB)。
   ru_maxrss 高水位增量在进程内不可靠:本进程先跑了 S1(10M 构建峰值 ~870MB),
   分片构建(~86MB)永远推不动高水位 → delta 恒 0。因此以"每片分配足迹"
   (fns×48 + triggers×16 + mods×64 + deps×16,精确、确定性)与高水位增量
   取 max 作为单分片构建峰值。 */
static long g_shard_peak_kb = 0;
static long g_shard_rss_peak_kb = 0;   /* 纯 ru_maxrss 增量(诊断用) */

long mosaic_bench_sharded_build_peak_kb(void) { return g_shard_peak_kb; }
long mosaic_bench_sharded_rss_delta_kb(void) { return g_shard_rss_peak_kb; }

int mosaic_bench_build_universe_sharded(const char *shard_dir, const char *so_path,
                                        u64 n_fns, u64 n_shards, u32 n_events, u32 triggers_per_fn) {
  char err[256];
  const char *ev_names[] = { "player_join", "block_break", "item_use", "entity_spawn", "tick" };
  if (n_events > 5) n_events = 5;
  if (!shard_dir || !so_path || n_shards == 0) {
    fprintf(stderr, "sharded universe build: bad args (dir=%s so=%s shards=%llu)\n",
            shard_dir ? shard_dir : "(null)", so_path ? so_path : "(null)",
            (unsigned long long)n_shards);
    return -1;
  }
  u64 per_shard_fns = n_fns / n_shards;   /* 每片函数数(余数并入末片) */
  if (per_shard_fns < 10) {
    fprintf(stderr, "sharded universe build: %llu fns / %llu shards too small (need >=10 per shard)\n",
            (unsigned long long)n_fns, (unsigned long long)n_shards);
    return -1;
  }
  if (mkdir(shard_dir, 0755) != 0 && errno != EEXIST) {
    fprintf(stderr, "sharded universe build: cannot create %s: %s\n", shard_dir, strerror(errno));
    return -1;
  }

  g_shard_peak_kb = 0;
  long high_water = bench_maxrss_kb();     /* 构建循环前的进程高水位 */
  double t_start = mosaic_bench_now_us();
  u64 mod_base = 0;                        /* 全局模块 id 游标:分片 k 独占 (mod_base, mod_base+n_mods] */

  for (u64 k = 0; k < n_shards; k++) {
    u64 n_fns_k = (k == n_shards - 1) ? (n_fns - k * per_shard_fns) : per_shard_fns;
    u64 n_mods = (n_fns_k + 9) / 10;       /* 每模块 10 函数(与 build_universe 同构) */
    char path[512];
    snprintf(path, sizeof path, "%s/shard_%03llu.pack", shard_dir, (unsigned long long)k);

    u64 n_triggers = n_fns_k * triggers_per_fn;
    u64 n_deps = n_mods > 1 ? n_mods - 1 : 0;
    mosaic_pack_builder *b = mosaic_pack_builder_create(path, n_mods, n_fns_k, n_triggers, n_deps, n_events);
    if (!b) {
      fprintf(stderr, "sharded universe build: shard %llu: create failed\n", (unsigned long long)k);
      return -1;
    }
    for (u32 i = 0; i < n_events; i++) mosaic_pack_builder_add_event(b, ev_names[i]);

    /* 每片独立确定性 rng(与 build_universe 同款 xorshift64,分片种子派生) */
    u64 rng = 0x9E3779B97F4A7C15ull ^ (k * 0x100000001B3ull);
    char name[64], so[256];
    u64 *mod_ids = malloc(n_mods * sizeof *mod_ids);
    if (!mod_ids) {
      fprintf(stderr, "sharded universe build: shard %llu: out of memory\n", (unsigned long long)k);
      mosaic_pack_builder_free(b);
      return -1;
    }
    for (u64 i = 0; i < n_mods; i++) {
      u64 mid = mod_base + 1 + i;          /* 全局连续模块 id */
      mod_ids[i] = mid;
      snprintf(name, sizeof name, "mod_%llu", (unsigned long long)mid);
      snprintf(so, sizeof so, "%s", so_path);
      mosaic_pack_builder_add_module(b, mid, 1, name, so);
    }
    /* 函数:fn_id = (module<<32)|local,每模块 10 个;state 64B */
    for (u64 i = 0; i < n_fns_k; i++) {
      u64 mi = i / 10;
      u32 code = (u32)(mosaic_bench_rng(&rng) % 3);
      u32 cost = (u32)(mosaic_bench_rng(&rng) % 16);
      mosaic_pack_builder_add_fn(b, mod_ids[mi], i % 10, code, 64, 1, cost,
                                 MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    }
    /* 触发:每函数订阅 triggers_per_fn 个事件 */
    for (u64 i = 0; i < n_fns_k; i++) {
      u64 mi = i / 10;
      u64 fn_id = (mod_ids[mi] << 32) | (i % 10);
      for (u32 t = 0; t < triggers_per_fn; t++) {
        u32 e = (u32)(mosaic_bench_rng(&rng) % n_events);
        mosaic_pack_builder_add_trigger(b, e, fn_id);
      }
    }
    /* 依赖链:片内模块 i → i-1(与 build_universe 同构,不跨片) */
    for (u64 i = 1; i < n_mods; i++)
      mosaic_pack_builder_add_dep(b, mod_ids[i], mod_ids[i - 1]);

    int rc = mosaic_pack_builder_finish(b, err, sizeof err);
    mosaic_pack_builder_free(b);
    free(mod_ids);
    if (rc) {
      fprintf(stderr, "sharded universe build: shard %llu: %s\n", (unsigned long long)k, err);
      return -1;
    }

    /* 每片构建后取高水位增量与精确分配足迹,记录 max(单分片构建峰值) */
    long cur = bench_maxrss_kb();
    long rss_delta = 0;
    if (cur > high_water) { rss_delta = cur - high_water; high_water = cur; }
    if (rss_delta > g_shard_rss_peak_kb) g_shard_rss_peak_kb = rss_delta;
    long footprint = (long)((n_fns_k * (u64)FN_SIZE + n_triggers * (u64)MT_SIZE +
                             n_mods * (u64)MM_SIZE + n_deps * (u64)MD_SIZE) / 1024);
    long peak = rss_delta > footprint ? rss_delta : footprint;
    if (peak > g_shard_peak_kb) g_shard_peak_kb = peak;

    double dt = (mosaic_bench_now_us() - t_start) / 1e6;
    printf("  shard %llu/%llu built (%.2fs)\n",
           (unsigned long long)(k + 1), (unsigned long long)n_shards, dt);
    fflush(stdout);
    mod_base += n_mods;
  }
  return 0;
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
