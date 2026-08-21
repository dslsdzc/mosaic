/* src/trigger.c — Task 8:触发索引区间扫描 + 事件派发 */
#include "mosaic_internal.h"
#include <stdio.h>

/* 触发表下界:第一个 event_id >= ev 的条目下标 */
static u64 trigger_lower_bound(mosaic_runtime *rt, u32 ev) {
  u64 n = hdr_trigger_count(rt->map);
  const mosaic_trigger_entry *t = (const mosaic_trigger_entry *)(rt->map + hdr_trigger_off(rt->map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    if (mt_event_id(&t[mid]) < ev) lo = mid + 1; else hi = mid;
  }
  return lo;
}

u32 mosaic_event_dispatch(mosaic_runtime *rt, u32 event_id, const void *event) {
  if (!rt) return 0;
  u32 executed = 0;
  u64 n = hdr_trigger_count(rt->map);          /* 计数不变,循环前读一次 */
  u64 i = trigger_lower_bound(rt, event_id);
  while (i < n) {
    /* 每次迭代现算 t:mod 回调可能墓碑(内部 mremap MAYMOVE 移动 rt->map),
       跨 execute 缓存表指针会悬垂(同线程重入同样崩)——t 必须从最新 rt->map 推导 */
    const mosaic_trigger_entry *t =
        (const mosaic_trigger_entry *)(rt->map + hdr_trigger_off(rt->map));
    if (mt_event_id(&t[i]) != event_id) break;
    u64 fn_id = mt_fn_id(&t[i]);
    mosaic_fn_obj *fn = ws_find(rt, fn_id);
    if (!fn) {
      fn = mosaic_fn_materialize(rt, fn_id);
      if (!fn) {   /* 降级:跳过 + 诊断 */
        fprintf(stderr, "mosaic: dispatch event %u: skip fn %llu (err %u)\n",
                event_id, (unsigned long long)fn_id, rt->last_err);
        i++;
        continue;
      }
    }
    fn->last_use = now_ns();
    fn->freq++;
    mosaic_fn_execute(fn, event_id, event);
    executed++;
    i++;
  }
  return executed;
}
