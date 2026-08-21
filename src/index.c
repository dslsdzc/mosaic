/* src/index.c — M1 索引查询,Task 4 实现 */
#include "mosaic_internal.h"
#include <string.h>

const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id) {
  if (!rt) return NULL;
  u64 n = hdr_module_count(rt->map);
  const mosaic_module_record *mods = (const mosaic_module_record *)(rt->map + hdr_module_off(rt->map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mm_id(&mods[mid]);
    if (id == module_id) return &mods[mid];
    if (id < module_id) lo = mid + 1; else hi = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}

const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  u32 module_id = (u32)(fn_id >> 32);
  const mosaic_module_record *m = mosaic_runtime_find_module(rt, module_id);
  if (!m) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const mosaic_function_record *fns = (const mosaic_function_record *)(rt->map + hdr_fn_off(rt->map));
  u32 base = mm_fn_base(m), cnt = mm_fn_count(m);
  u64 lo = 0, hi = cnt;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mf_id(&fns[base + mid]);
    if (id == fn_id) return &fns[base + mid];
    if (id < fn_id) lo = mid + 1; else hi = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}
