#ifndef MOSAIC_EVENT_H
#define MOSAIC_EVENT_H
#include "mosaic/base.h"
struct mosaic_runtime;
u32 mosaic_event_dispatch(struct mosaic_runtime *rt, u32 event_id, const void *event);  /* Task 8 实现 */
#endif
