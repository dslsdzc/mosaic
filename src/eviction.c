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
     flush(此时无模块代码在栈上——驱逐自身不执行模块代码,安全) */
  flush_pending_dlclose(rt);
  return n;
}
