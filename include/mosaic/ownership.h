#ifndef MOSAIC_OWNERSHIP_H
#define MOSAIC_OWNERSHIP_H
#include "mosaic/base.h"
struct mosaic_runtime;
typedef struct mosaic_lease mosaic_lease;
mosaic_lease *mosaic_lease_acquire(struct mosaic_runtime *rt, u64 fn_id);  /* Task 9 实现 */
void mosaic_lease_release(mosaic_lease *l);                                /* Task 9 实现 */
#endif
