/* src/trigger.c — Task 8:触发索引区间扫描 + 事件派发;M1.5-A:跨 pack 派发 */
#include "mosaic_internal.h"
#include <stdio.h>

/* 触发表下界:第一个 event_id >= ev 的条目下标(逐 pack 扫描用)。
   M6-B:去 static,声明入 mosaic_internal.h——bridge.c(triggerSubscribers
   列出事件订阅者)复用同一区间扫描,避免复制二分逻辑。 */
u64 trigger_lower_bound(const u8 *map, u32 ev) {
  u64 n = hdr_trigger_count(map);
  const mosaic_trigger_entry *t = (const mosaic_trigger_entry *)(map + hdr_trigger_off(map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    if (mt_event_id(&t[mid]) < ev) lo = mid + 1; else hi = mid;
  }
  return lo;
}

/* M1.5-A:遍历 pack。每个 pack 内部逻辑与单 pack 版一致:计数不变,循环前读
   一次;t 指针每次迭代从 pack_map(rt, p) 现算(重入纪律)——mod 回调可能墓碑,
   内部 mremap(MAYMOVE)会移动本 pack 映射(只动本 pack;其他 pack 指针不受
   影响,但统一现算更稳),跨 execute 缓存表指针会悬垂。返回值 = 跨 pack
   执行总数。触发条目可引用任意 pack 的函数(合并索引按 fn_id 全局解析)。 */
u32 mosaic_event_dispatch(mosaic_runtime *rt, u32 event_id, const void *event) {
  if (!rt) return 0;
  rt->dispatch_depth++;
  u32 executed = 0;
  /* M9:超时预算——入口快照(setter 由 agent premain 线程于首次派发前设置,
     与派发无并发窗口;运行期再 set 亦在派发外,单线程前提适用)。仅 budget != 0
     才取时钟 + 入口清 last_err(预算生效时每次派发后 last_error 反映本次派发
     结果,避免陈旧 TIMEOUT 被 agent 告警误报);预算 0 时零开销:不取时钟、
     不触碰 last_err(与既有行为完全一致)。 */
  u64 budget_us = rt->dispatch_budget_us;
  u64 start_ns = budget_us ? now_ns() : 0;
  if (budget_us) rt->last_err = MOSAIC_OK;
  for (size_t p = 0; p < rt->n_packs; p++) {
    u8 *map = pack_map(rt, p);
    u64 n = hdr_trigger_count(map);          /* 计数不变,循环前读一次 */
    u64 i = trigger_lower_bound(map, event_id);
    while (i < n) {
      /* M9:订阅者边界超时检查(执行前)。超时 → 跳过剩余订阅者、返回已执行数、
         last_err = MOSAIC_ERR_TIMEOUT。只能保护"慢函数不阻塞同事件其他订阅者",
         不能中断正在执行的函数(原生代码不可安全取消)。 */
      if (budget_us && now_ns() - start_ns > budget_us * 1000ull) {
        rt->last_err = MOSAIC_ERR_TIMEOUT;
        goto timeout;
      }
      /* 每次迭代现算 map/t:mod 回调可能墓碑(内部 mremap MAYMOVE 移动本 pack
         映射),跨 execute 缓存表指针会悬垂(同线程重入同样崩) */
      map = pack_map(rt, p);
      const mosaic_trigger_entry *t =
          (const mosaic_trigger_entry *)(map + hdr_trigger_off(map));
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
  }
timeout:
  /* 缺陷 2 安全点:flush pending dlclose 只在最外层派发末尾(此时所有 execute
     已返回,栈上无任何模块代码帧,卸载 .so 不悬垂)。mod 回调可能嵌套派发,
     内层末尾的栈上仍压着外层回调的模块代码帧,故只减深度不 flush。 */
  rt->dispatch_depth--;
  if (rt->dispatch_depth == 0) flush_pending_dlclose(rt);
  return executed;
}
