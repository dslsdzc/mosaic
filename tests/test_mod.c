/* 测试用合成模块:3 个代码入口,state = 64B(首 u32 为计数器) */
#include "mosaic/module.h"

static void code_inc(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) ((u32 *)s)[0]++;
}
static void code_add(void *s, u32 e, const void *ev) {
  (void)e;
  if (s) ((u32 *)s)[0] += ev ? *(const u32 *)ev : 1u;
}
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

static const mosaic_fn_entry g_fns[3] = {
  { 0, code_inc },
  { 1, code_add },
  { 2, code_noop },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 3, g_fns, 64 };
  return &abi;
}
