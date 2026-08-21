#include "synth_universe.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mosaic/eviction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void gate(const char *name, int pass, const char *detail) {
  printf("GATE %-4s %s: %s\n", name, pass ? "PASS" : "FAIL", detail);
  if (!pass) g_fail = 1;
}

/* 阈值(来自设计规格第 9 节) */
#define GATE_S1_RSS_MB 80.0
#define GATE_S3_CYCLE_US 500.0
#define GATE_S4_RATIO 1.10

int main(int argc, char **argv) {
  u64 n_fns = argc > 1 ? strtoull(argv[1], NULL, 10) : 10000000ull;
  const char *pack = argc > 2 ? argv[2] : "bench/synth_10m.pack";
  const char *solo_pack = argc > 3 ? argv[3] : "bench/solo.pack";
  const char *cold_pack = argc > 4 ? argv[4] : "bench/cold.pack";
  const char *so = argc > 5 ? argv[5] : "build/bench/synth_mod.so";
  u64 n_modules = n_fns / 10;   /* 每模块 10 函数 */
  char err[256];

  /* ---- S1:冷规模 ---- */
  double t0 = mosaic_bench_now_us();
  if (mosaic_bench_build_universe(pack, so, n_fns, n_modules, 5, 2) != 0) {
    gate("S1", 0, "universe build failed"); return 1;
  }
  double t_build_s = (mosaic_bench_now_us() - t0) / 1e6;
  long rss_before = mosaic_bench_rss_kb();
  mosaic_runtime *rt = mosaic_runtime_open(pack, err, sizeof err);
  if (!rt) { gate("S1", 0, err); return 1; }
  long rss_after = mosaic_bench_rss_kb();
  double rss_mb = (double)(rss_after - rss_before) / 1024.0;
  /* 触碰 1k 个冷记录(模拟索引查询,不物化) */
  u64 seed = 7;
  for (int i = 0; i < 1000; i++) {
    u64 fn_id = (mosaic_bench_rng(&seed) % n_fns);
    const mosaic_function_record *r = mosaic_runtime_find_function(rt, fn_id);
    (void)r;
  }
  long rss_after_q = mosaic_bench_rss_kb();
  double rss_q_mb = (double)(rss_after_q - rss_before) / 1024.0;
  char detail[256];
  snprintf(detail, sizeof detail, "build %.2fs, fns=%llu, RSS delta %.2f MB (query %.2f MB), limit %.0f MB",
           t_build_s, (unsigned long long)n_fns, rss_mb, rss_q_mb, GATE_S1_RSS_MB);
  gate("S1", rss_q_mb <= GATE_S1_RSS_MB, detail);
  mosaic_runtime_close(rt);

  /* ---- S2:冷启动(诊断,非门禁)——专用 1k 函数冷包,避免在 10M 宇宙上
     物化数百万函数(单事件订阅 ~400 万,会把 RSS 打爆) ---- */
  if (mosaic_bench_build_cold(cold_pack, so) != 0) { gate("S2", 0, "cold build failed"); return 1; }
  mosaic_runtime *rc2 = mosaic_runtime_open(cold_pack, err, sizeof err);
  if (!rc2) { gate("S2", 0, err); return 1; }
  u32 ev_cold = mosaic_runtime_event_id(rc2, "cold");
  if (ev_cold == MOSAIC_U32_NONE) { gate("S2", 0, "cold event not found"); return 1; }
  t0 = mosaic_bench_now_us();
  u32 executed = mosaic_event_dispatch(rc2, ev_cold, NULL);
  double t_s2 = mosaic_bench_now_us() - t0;
  printf("S2 DIAG: cold dispatch -> %u materialized+executed, %.1f us total, %.2f us/fn\n",
         executed, t_s2, executed ? t_s2 / executed : 0.0);
  mosaic_evict_config zcfg = { 0 };
  mosaic_evict_idle(rc2, &zcfg);
  mosaic_runtime_close(rc2);

  /* ---- S3:全循环(物化→执行→墓碑→恢复→执行) ---- */
  if (mosaic_bench_build_solo(solo_pack, so) != 0) { gate("S3", 0, "solo build failed"); return 1; }
  mosaic_runtime *rs = mosaic_runtime_open(solo_pack, err, sizeof err);
  if (!rs) { gate("S3", 0, err); return 1; }
  u32 ev_solo = mosaic_runtime_event_id(rs, "solo");
  t0 = mosaic_bench_now_us();
  mosaic_event_dispatch(rs, ev_solo, NULL);      /* 物化 + 执行 */
  mosaic_evict_idle(rs, &zcfg);                  /* 墓碑 */
  mosaic_event_dispatch(rs, ev_solo, NULL);      /* 恢复 + 执行 */
  double t_cycle_us = mosaic_bench_now_us() - t0;
  snprintf(detail, sizeof detail, "full cycle %.1f us, limit %.0f us", t_cycle_us, GATE_S3_CYCLE_US);
  gate("S3", t_cycle_us <= GATE_S3_CYCLE_US, detail);

  /* ---- S4:热路径 vs 直调 ---- */
  mosaic_fn_obj *fn = mosaic_fn_materialize(rs, 42ull << 32);
  if (!fn) { gate("S4", 0, "materialize failed"); return 1; }
  const int ITERS = 5000000;
  volatile u32 sink = 0;
  t0 = mosaic_bench_now_us();
  for (int i = 0; i < ITERS; i++) mosaic_fn_execute(fn, ev_solo, NULL);
  double t_exec = mosaic_bench_now_us() - t0;
  t0 = mosaic_bench_now_us();
  for (int i = 0; i < ITERS; i++) { fn->code(fn->state, ev_solo, NULL); sink += (u32)i; }
  double t_direct = mosaic_bench_now_us() - t0;
  (void)sink;
  double ratio = t_exec / t_direct;
  snprintf(detail, sizeof detail, "execute %.1f ns/call, direct %.1f ns/call, ratio %.3f, limit %.2f",
           t_exec * 1000.0 / ITERS, t_direct * 1000.0 / ITERS, ratio, GATE_S4_RATIO);
  gate("S4", ratio <= GATE_S4_RATIO, detail);
  mosaic_runtime_close(rs);

  printf(g_fail ? "\nGATES FAILED\n" : "\nALL GATES PASSED\n");
  return g_fail ? 1 : 0;
}
