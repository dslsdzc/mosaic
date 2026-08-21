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

/* M1.5-B:分片宇宙构建器。n_shards 个分片 pack 落在 shard_dir/shard_%03zu.pack,
   每个分片 n_fns/n_shards 个函数;模块 id 从全局连续范围切分(分片 k 独占
   [k*per+1, (k+1)*per],per = 每片模块数),事件表每片相同(宇宙级全局命名
   空间),so_path 每片相同。返回 0 成功,-1 失败(stderr 说明)。 */
int mosaic_bench_build_universe_sharded(const char *shard_dir, const char *so_path,
                                        u64 n_fns, u64 n_shards, u32 n_events, u32 triggers_per_fn);
/* 最近一次分片构建中"单分片构建峰值"(KB):max(每片分配足迹,
   每片构建后的进程高水位增量)。高水位增量在 S1 之后恒为 0(S1 峰值更高),
   故以精确分配足迹为主。 */
long mosaic_bench_sharded_build_peak_kb(void);
/* 纯 ru_maxrss 高水位增量(诊断用) */
long mosaic_bench_sharded_rss_delta_kb(void);
#endif
