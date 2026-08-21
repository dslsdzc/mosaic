/* 合成模块:10M 函数共享 3 个代码入口;state = 64B */
#include "mosaic/module.h"

static void synth_inc(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) { u32 *st = s; st[0]++; }
}
static void synth_add(void *s, u32 e, const void *ev) {
  (void)e;
  if (s) { u32 *st = s; st[0] += ev ? *(const u32 *)ev : 1u; }
}
static void synth_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

static const mosaic_fn_entry g_fns[3] = {
  { 0, synth_inc }, { 1, synth_add }, { 2, synth_noop },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 3, g_fns, 64, 0, NULL };
  return &abi;
}
