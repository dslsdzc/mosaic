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

/* ---- M2-2b:活跃代记录解析 ---- */

/* 活跃代 = gen_route 命中(该 fn 被某补丁更新,走补丁 pack 记录);未命中 =
   基础 pack 原始记录(generation 最低)。out_pack 编码:n_packs + tx_packs
   下标(补丁记录),pack_view() 统一解析。 */
const mosaic_function_record *find_function_active(mosaic_runtime *rt, u64 fn_id, size_t *out_pack) {
  if (!rt) return NULL;
  u32 gen = rt->routes ? gen_route_get(rt->routes, fn_id) : 0;
  if (!gen) return find_function_ex(rt, fn_id, out_pack);
  /* gen_route 命中:按 fn_id 在 tx_packs 中二分(补丁每 fn 单条,begin 已校验;
     核对 mf_generation == 路由代,防御性)。tx_packs 顺序 = commit 顺序,
     逐个 pack 查(补丁 pack 通常很小)。 */
  for (size_t i = 0; i < rt->n_tx_packs; i++) {
    const u8 *map = rt->tx_packs[i].map;
    u64 n = hdr_fn_count(map);
    const mosaic_function_record *fns = (const mosaic_function_record *)(map + hdr_fn_off(map));
    u64 lo = 0, hi = n;
    while (lo < hi) {
      u64 mid = lo + (hi - lo) / 2;
      u64 id = mf_id(&fns[mid]);
      if (id == fn_id) {
        if (mf_generation(&fns[mid]) == gen) {
          if (out_pack) *out_pack = rt->n_packs + i;
          return &fns[mid];
        }
        break;   /* 同 fn_id 但代不符(不一致补丁):继续找下一 pack */
      }
      if (id < fn_id) lo = mid + 1; else hi = mid;
    }
  }
  /* 路由命中但未定位记录(防御,正常流程不可达):回落基础记录 */
  return find_function_ex(rt, fn_id, out_pack);
}

/* 活跃模块记录:已 commit 补丁优先(so_path/version 等以补丁为准——模块记录
   v2 的 so_path 可能与 base 不同,mod_load 必须用补丁的),未命中回落基础。
   out_pack 编码同 find_function_active。
   M2-2b 修复(I-2):**反向扫描**——tx_packs 顺序 = commit 顺序(旧→新),顺序
   扫描会返回最早的补丁记录:多补丁下模块被 patch1/patch2 先后更新时,gen3
   的物化会拿到 patch1 的 so_path(旧 .so)。最新补丁优先:从最新到最旧扫描,
   返回第一个匹配(模块版本/so_path 以最新补丁为准)。 */
const mosaic_module_record *find_module_active(mosaic_runtime *rt, u64 module_id, size_t *out_pack) {
  if (!rt) return NULL;
  for (size_t i = rt->n_tx_packs; i > 0; i--) {
    const u8 *map = rt->tx_packs[i - 1].map;
    u64 n = hdr_module_count(map);
    const mosaic_module_record *mods = (const mosaic_module_record *)(map + hdr_module_off(map));
    u64 lo = 0, hi = n;
    while (lo < hi) {
      u64 mid = lo + (hi - lo) / 2;
      u64 id = mm_id(&mods[mid]);
      if (id == module_id) { if (out_pack) *out_pack = rt->n_packs + i - 1; return &mods[mid]; }
      if (id < module_id) lo = mid + 1; else hi = mid;
    }
  }
  return find_module_ex(rt, module_id, out_pack);
}
