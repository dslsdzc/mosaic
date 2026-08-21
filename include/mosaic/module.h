#ifndef MOSAIC_MODULE_H
#define MOSAIC_MODULE_H
#include "mosaic/base.h"

#define MOSAIC_MODULE_ABI_VERSION 1

typedef void (*mosaic_code_fn)(void *state, u32 event_id, const void *event);

typedef struct { u32 code_off; mosaic_code_fn code; } mosaic_fn_entry;
typedef struct {
  u32 abi_version;
  u32 fn_count;
  const mosaic_fn_entry *fns;   /* code_off 索引到这张表 */
  u32 state_size;               /* 默认 state 大小(供无 hint 的函数) */
} mosaic_module_abi;

typedef const mosaic_module_abi *(*mosaic_module_abi_v1_fn)(void);
#endif
