#include "mosaic_internal.h"

int mosaic_evict_idle(mosaic_runtime *rt, const mosaic_evict_config *cfg) {
  if (!rt || !cfg) return 0;
  u64 now = now_ns();
  int n = 0;
  for (mosaic_fn_obj *f = rt->ws_head; f; ) {
    mosaic_fn_obj *nx = f->next;
    /* 驱逐只看效用窗口与租约,不碰生命周期状态(正交轴) */
    if (!f->refs && (f->last_use + cfg->window_ns) <= now) {
      if (mosaic_fn_tombstone(rt, f) == 0) n++;
    }
    f = nx;
  }
  /* 缺陷 2 安全点:墓碑可能使模块 refs 归零进入 pending,在驱逐循环末尾统一
     flush。驱逐自身不执行模块代码,但若本调用发生在派发途中(mod 回调内调用
     mosaic_evict_idle 且 window=0,执行中的 fn 可被驱逐),栈上仍压着模块代码
     帧——此时只减派发深度,由最外层 dispatch 末尾 flush,否则 dlclose 正在
     执行的 .so 会使回调返回地址悬垂(与 trigger.c dispatch 末尾防护对称)。 */
  if (rt->dispatch_depth == 0) flush_pending_dlclose(rt);
  return n;
}
