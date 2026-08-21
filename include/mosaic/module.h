#ifndef MOSAIC_MODULE_H
#define MOSAIC_MODULE_H
#include "mosaic/base.h"

#define MOSAIC_MODULE_ABI_VERSION 2

typedef void (*mosaic_code_fn)(void *state, u32 event_id, const void *event);
/* v2:状态迁移钩子——commit 阶段把 v1 代 state 原地转换为 v2 代(size 字节)。
   索引按函数记录的 reserved 字段(transform_index+1)引用,见 pack.h FN_OFF_RSVD。 */
typedef void (*mosaic_state_transform)(const void *v1_state, void *v2_state, u32 size);

typedef struct { u32 code_off; mosaic_code_fn code; } mosaic_fn_entry;
typedef struct {
  u32 abi_version;
  u32 fn_count;
  const mosaic_fn_entry *fns;   /* code_off 索引到这张表 */
  u32 state_size;               /* 默认 state 大小(供无 hint 的函数) */
  u32 transform_count;                  /* v2:状态迁移钩子表条数 */
  const mosaic_state_transform *transforms;  /* v2:钩子表,按索引取(0-based) */
} mosaic_module_abi;

typedef const mosaic_module_abi *(*mosaic_module_abi_v1_fn)(void);
#endif
