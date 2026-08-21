/* 测试用合成模块:4 个代码入口,state = 64B(u32 counter; u32 last_event) */
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
static const mosaic_fn_entry g_fns[4] = {
  { 0, code_inc }, { 1, code_add }, { 2, code_noop }, { 3, code_hook },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 4, g_fns, 64, 0, NULL };
  return &abi;
}
