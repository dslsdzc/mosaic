#ifndef MOSAIC_FUNCTION_H
#define MOSAIC_FUNCTION_H
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/module.h"

struct slab;
typedef struct mosaic_fn_obj {
  u64 fn_id;
  mosaic_code_fn code;      /* 热路径直接调用 */
  void *state;              /* 热路径直接传入 */
  u32 refs;                 /* 租约 + 在途 */
  u64 last_use;             /* Denning 窗口追踪 */
  u32 freq;                 /* GDSF-lite */
  u32 state_size;
  struct mosaic_fn_obj *prev, *next;   /* 窗口链表 */
  struct slab *slab;
  const mosaic_function_record *rec;   /* 回指 mmap 记录 */
} mosaic_fn_obj;

struct mosaic_runtime;
mosaic_fn_obj *mosaic_fn_materialize(struct mosaic_runtime *rt, u64 fn_id);
void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event);
int mosaic_fn_tombstone(struct mosaic_runtime *rt, mosaic_fn_obj *fn);
#endif
