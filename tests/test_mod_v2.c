/* 测试用合成模块 v2(M2-2b 修复回归):code_off 0..3 同 test_mod 语义(合成模块
   ABI 的通用入口),code_off 4 = v2inc7(counter += 7)——补丁把 fn 的 code_off
   指到 4 后,物化必须执行**新 .so**(test_mod_v2)的 +7 语义,而非 mods 缓存
   里的旧 test_mod(+2)。transform[0] = ×10 迁移钩子(与 test_mod 一致)。
   注:任务简报写"transforms 空",但三个回归用例的数值契约(30/37、10/17、
   170/183)均要求 commit 迁移的 ×10 由补丁 so 的 abi 提供(transform 索引
   经 validate 对补丁模块 abi 校验、迁移经补丁模块探测调用),故以数值契约
   为准导出 transform_x10——详见 task-m2-2b-report.md。 */
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

/* code_off 3:从 state 读 (rt, fn, hook) 指针并调用 hook(rt, fn)——与 test_mod
   同语义(重入回归测试用) */
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
/* code_off 4(M2-2b 修复回归可观察行为):counter += 7 */
static void v2inc7(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) { u32 *st = s; st[0] += 7; }
}
/* 状态迁移钩子:整块拷贝 + counter ×10(未覆盖字段原样保留,供 demote 语义) */
static void transform_x10(const void *v1, void *v2, u32 size) {
  if (!v1 || !v2 || size < 4) return;
  memcpy(v2, v1, size);
  ((u32 *)v2)[0] *= 10;
}
static const mosaic_fn_entry g_fns[5] = {
  { 0, code_inc }, { 1, code_add }, { 2, code_noop }, { 3, code_hook }, { 4, v2inc7 },
};
static const mosaic_state_transform g_transforms[1] = { transform_x10 };

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 5, g_fns, 64, 1, g_transforms };
  return &abi;
}
