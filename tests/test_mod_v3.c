/* 测试用合成模块 v3(M2-2b 修复回归):与 test_mod_v2 同结构——code_off 0..3
   同 test_mod 语义,code_off 4 = v3inc13(counter += 13),transform[0] = ×10
   迁移钩子。用途:多补丁场景验证 find_module_active 必须解析到**最新**补丁
   的 so_path(test_mod_v3),而非最早的(test_mod_v2)。 */
#include "mosaic/module.h"
#include <string.h>

/* state 布局:u32 counter; u32 last_event(同 test_mod) */
static void code_inc(void *s, u32 e, const void *ev) {
  (void)ev;
  if (s) { u32 *st = s; st[0]++; st[1] = e; }
}
static void code_add(void *s, u32 e, const void *ev) {
  if (s) { u32 *st = s; st[0] += ev ? *(const u32 *)ev : 1u; st[1] = e; }
}
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

/* code_off 3:与 test_mod 同语义(重入回归测试用) */
typedef void (*mosaic_test_hook)(void *rt, void *fn);
static void code_hook(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (!s) return;
  u8 *b = s; void *rt = NULL, *fn = NULL; mosaic_test_hook h = NULL;
  memcpy(&rt, b + 0, sizeof rt);
  memcpy(&fn, b + 8, sizeof fn);
  memcpy(&h, b + 16, sizeof h);
  if (h) h(rt, fn);
}
/* code_off 4(M2-2b 修复回归可观察行为):counter += 13 */
static void v3inc13(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) { u32 *st = s; st[0] += 13; }
}
/* 状态迁移钩子:整块拷贝 + counter ×10(与 test_mod/test_mod_v2 一致) */
static void transform_x10(const void *v1, void *v2, u32 size) {
  if (!v1 || !v2 || size < 4) return;
  memcpy(v2, v1, size);
  ((u32 *)v2)[0] *= 10;
}
static const mosaic_fn_entry g_fns[5] = {
  { 0, code_inc }, { 1, code_add }, { 2, code_noop }, { 3, code_hook }, { 4, v3inc13 },
};
static const mosaic_state_transform g_transforms[1] = { transform_x10 };

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 5, g_fns, 64, 1, g_transforms };
  return &abi;
}
