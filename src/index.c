/* src/index.c — M1 索引查询,Task 4 实现;M1.5-A 改为范围表 → pack 表两级二分 */
#include "mosaic_internal.h"
#include <string.h>

/* M1.5-A:范围表上二分。packs 按 (min,max) 升序;先找"最后一个 min ≤ id"的 pack
   (谓词 min ≤ id 单调,二分下界),再核对 id ≤ max——空范围 (1,0) 永不命中
   (max < min ≤ id ⇒ max ≥ id 不成立)。命中后在该 pack 模块表上普通二分。
   找到时经 out_pack 输出 pack 下标。 */
const mosaic_module_record *find_module_ex(mosaic_runtime *rt, u64 module_id, size_t *out_pack) {
  if (!rt) return NULL;
  u64 lo = 0, hi = rt->n_packs;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    if (rt->packs[mid].min_mod <= module_id) lo = mid + 1; else hi = mid;
  }
  if (lo == 0) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  struct pack_view *pv = &rt->packs[lo - 1];
  if (pv->max_mod < module_id) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const u8 *m = pv->map;
  u64 n = hdr_module_count(m);
  const mosaic_module_record *mods = (const mosaic_module_record *)(m + hdr_module_off(m));
  u64 a = 0, b = n;
  while (a < b) {
    u64 mid = a + (b - a) / 2;
    u64 id = mm_id(&mods[mid]);
    if (id == module_id) { if (out_pack) *out_pack = (size_t)(lo - 1); return &mods[mid]; }
    if (id < module_id) a = mid + 1; else b = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}

const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id) {
  return find_module_ex(rt, module_id, NULL);
}

const mosaic_function_record *find_function_ex(mosaic_runtime *rt, u64 fn_id, size_t *out_pack) {
  if (!rt) return NULL;
  u32 module_id = (u32)(fn_id >> 32);
  size_t pack = 0;
  const mosaic_module_record *m = find_module_ex(rt, module_id, &pack);
  if (!m) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const u8 *map = rt->packs[pack].map;
  const mosaic_function_record *fns = (const mosaic_function_record *)(map + hdr_fn_off(map));
  u32 base = mm_fn_base(m), cnt = mm_fn_count(m);
  u64 lo = 0, hi = cnt;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mf_id(&fns[base + mid]);
    if (id == fn_id) { if (out_pack) *out_pack = pack; return &fns[base + mid]; }
    if (id < fn_id) lo = mid + 1; else hi = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}

const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id) {
  return find_function_ex(rt, fn_id, NULL);
}
