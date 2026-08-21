#ifndef MOSAIC_SYNTH_UNIVERSE_H
#define MOSAIC_SYNTH_UNIVERSE_H
#include "mosaic/base.h"

/* 确定性 xorshift64 */
static inline u64 mosaic_bench_rng(u64 *s) {
  *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
  return *s;
}

int mosaic_bench_build_universe(const char *pack_path, const char *so_path,
                                u64 n_fns, u64 n_modules, u32 n_events, u32 triggers_per_fn);
int mosaic_bench_build_solo(const char *pack_path, const char *so_path);
int mosaic_bench_build_cold(const char *pack_path, const char *so_path);
long mosaic_bench_rss_kb(void);
double mosaic_bench_now_us(void);
#endif
