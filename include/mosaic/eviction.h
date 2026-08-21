#ifndef MOSAIC_EVICTION_H
#define MOSAIC_EVICTION_H
#include "mosaic/base.h"
struct mosaic_runtime;
typedef struct { u64 window_ns; } mosaic_evict_config;
int mosaic_evict_idle(struct mosaic_runtime *rt, const mosaic_evict_config *cfg);  /* Task 9 实现 */
#endif
