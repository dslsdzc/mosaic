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
  u32 pack;                 /* 保留/诊断用:物化时记录的归属 pack(只写不读)。
                              权威来源是 find_function_ex(按 fn_id 实时重派生
                              pack),墓碑路径即以它为准,勿依赖本字段 */
  struct mosaic_fn_obj *prev, *next;   /* 窗口链表 */
  struct slab *slab;
  const mosaic_function_record *rec;   /* 回指 mmap 记录 */
} mosaic_fn_obj;

struct mosaic_runtime;
mosaic_fn_obj *mosaic_fn_materialize(struct mosaic_runtime *rt, u64 fn_id);
/* 修正 D-10-4:热路径包装原为 out-of-line 函数(lifecycle.c,已编译成尾调用
   jmp),但外层 call/ret 每调用仍 ~1.3-2 周期,设计规格第 9 节 S4 门禁
   (≤ 直调 1.10×)本机实测稳定 ~1.13 超限。设计"热路径 = 一次指针解引用,
   零状态机检查" ⇒ 包装必须是 inline,与直调同构;门禁由此成为真正的回归
   防护——任何在包装里加检查都会立刻抬升 ratio。 */
static inline void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event) {
  fn->code(fn->state, event_id, event);
}
int mosaic_fn_tombstone(struct mosaic_runtime *rt, mosaic_fn_obj *fn);
#endif
