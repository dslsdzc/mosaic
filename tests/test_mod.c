/* 测试用合成模块:6 个代码入口,state = 64B(u32 counter; u32 last_event;
   u8 payload[56] 为 code_off 5 载荷探针使用);M2-2b:code_off 4 = v2 行为
   (code_v2inc,counter += 2),transform[0] = ×10 迁移钩子(供补丁事务的
   commit 状态迁移观察) */
#include "mosaic/module.h"
#include <string.h>

/* state 布局:u32 counter; u32 last_event */
static void code_inc(void *s, u32 e, const void *ev) {
  (void)ev;
  if (s) { u32 *st = s; st[0]++; st[1] = e; }
}
static void code_add(void *s, u32 e, const void *ev) {
  if (s) { u32 *st = s; st[0] += ev ? *(const u32 *)ev : 1u; st[1] = e; }
}
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

/* code_off 3:从 state 读 (rt, fn, hook) 指针并调用 hook(rt, fn)——供重入回归测试使用 */
typedef void (*mosaic_test_hook)(void *rt, void *fn);
static void code_hook(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (!s) return;
  u8 *b = s; void *rt = NULL, *fn = NULL; mosaic_test_hook h = NULL;
  memcpy(&rt, b + 0, sizeof rt);
  memcpy(&fn, b + 8, sizeof fn);
  memcpy(&h, b + 16, sizeof h);
  if (h) h(rt, fn);   /* 注意:调用后不再触碰 s(回调可能已墓碑并释放 state) */
}
/* code_off 4(M2-2b,v2 可观察行为):counter += 2 */
static void code_v2inc(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) { u32 *st = s; st[0] += 2; }
}
/* 状态迁移钩子:整块拷贝 + counter ×10(未覆盖字段原样保留,供 demote 语义) */
static void transform_x10(const void *v1, void *v2, u32 size) {
  if (!v1 || !v2 || size < 4) return;
  memcpy(v2, v1, size);
  ((u32 *)v2)[0] *= 10;
}
/* code_off 5(M3-2):事件载荷探针——世界动作/生成器把载荷暂存于 ≥64B 零化
   暂存缓冲(src/world.c world_stage),探针把前 56B memcpy 进 state。
   state 布局:{u32 counter; u32 last_event; u8 payload[56]}(64B 整);
   payload 字段全部 4 对齐 u32,供测试断言载荷 == 动作参数。 */
static void code_payload_probe(void *s, u32 e, const void *ev) {
  if (!s) return;
  u8 *b = s;
  ((u32 *)b)[0]++;
  ((u32 *)b)[1] = e;
  if (ev) memcpy(b + 8, ev, 56); else memset(b + 8, 0, 56);
}
static const mosaic_fn_entry g_fns[6] = {
  { 0, code_inc }, { 1, code_add }, { 2, code_noop }, { 3, code_hook }, { 4, code_v2inc },
  { 5, code_payload_probe },
};
static const mosaic_state_transform g_transforms[1] = { transform_x10 };

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 6, g_fns, 64, 1, g_transforms };
  return &abi;
}
