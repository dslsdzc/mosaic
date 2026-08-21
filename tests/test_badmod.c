#include "mosaic/module.h"
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }
static const mosaic_fn_entry g_fns[1] = { { 0, code_noop } };
const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { 999 /* 错误版本 */, 1, g_fns, 0 };
  return &abi;
}
