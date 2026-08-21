/* src/descriptor.c — M2-4:Item 描述符查询(纯冷态,零 dlopen)
 *
 * 所有查询只读 mmap 上的 item 记录表,返回指向 mmap 记录的描述符指针——
 * 不触碰 so 路径、不调用 dlopen、不物化。唯一物化入口是应用侧把
 * provider fn_id 交给 mosaic_fn_materialize。 */
#include "mosaic_internal.h"
#include <string.h>

/* 名字比较(长度感知,与构建期 it_cmp 同一语义):memcmp 前缀,相等比长度。
   name_off == 0 → 空串,排任何真实名字之前;字符串越界(损坏 pack)按空串
   处理并继续(防御,不崩)。map_len 界内扫描,避免无终止符串读穿映射。 */
static int it_name_cmp(const u8 *map, size_t map_len, u64 meta_base,
                       const mosaic_item_record *it, const char *name, size_t nl) {
  u32 o = mi_name_off(it);
  if (o == 0) return -1;
  if (meta_base + o >= map_len) return -1;
  const char *p = (const char *)(map + meta_base + o);
  size_t avail = map_len - (meta_base + o);
  size_t l = 0;
  while (l < avail && p[l] != '\0') l++;
  size_t c = l < nl ? l : nl;
  int r = memcmp(name, p, c);
  if (r == 0) r = (nl > l) - (nl < l);
  return r;
}

/* 表按 (category, name) 排序 → 分类连续区间。返回 [lo, hi) 内全部条目
   category == cat;区间空 → lo == hi。 */
static void item_cat_range(const u8 *map, u64 n, u32 cat, u64 *out_lo, u64 *out_hi) {
  const mosaic_item_record *items = (const mosaic_item_record *)(map + hdr_item_off(map));
  u64 lo = 0, hi = n;
  while (lo < hi) {                       /* 第一个 category >= cat */
    u64 mid = lo + (hi - lo) / 2;
    if (mi_category(&items[mid]) < cat) lo = mid + 1; else hi = mid;
  }
  u64 start = lo;
  hi = n;
  while (lo < hi) {                       /* 第一个 category > cat */
    u64 mid = lo + (hi - lo) / 2;
    if (mi_category(&items[mid]) <= cat) lo = mid + 1; else hi = mid;
  }
  *out_lo = start; *out_hi = lo;
}

u64 mosaic_item_count(mosaic_runtime *rt) {
  if (!rt) return 0;
  u64 total = 0;
  for (size_t i = 0; i < rt->n_packs; i++) total += hdr_item_count(rt->packs[i].map);
  return total;
}

/* 按 (category, name) 查找:每 pack 先二分分类区间,再在区间内按名字二分。
   名字空间全局,pack 顺序即优先级,返回首个命中的记录。未命中 → NULL +
   NOT_FOUND(与 find_module/find_function 同惯例)。 */
const mosaic_item_record *mosaic_item_by_name(mosaic_runtime *rt, u32 category, const char *name) {
  if (!rt || !name) return NULL;
  size_t nl = strlen(name);
  for (size_t i = 0; i < rt->n_packs; i++) {
    const u8 *map = rt->packs[i].map;
    u64 n = hdr_item_count(map);
    if (!n) continue;
    u64 lo, hi;
    item_cat_range(map, n, category, &lo, &hi);
    u64 meta_base = hdr_meta_off(map);
    const mosaic_item_record *items = (const mosaic_item_record *)(map + hdr_item_off(map));
    u64 a = lo, b = hi;
    while (a < b) {
      u64 mid = a + (b - a) / 2;
      int r = it_name_cmp(map, rt->packs[i].map_len, meta_base, &items[mid], name, nl);
      if (r == 0) return &items[mid];
      if (r < 0) b = mid; else a = mid + 1;   /* key < mid 条目 → 左半 */
    }
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}

/* 枚举某分类全部 item:每 pack 分类区间线性扫,跨 pack 顺序枚举;回调返回
   非 0 → 停止并透传该值。 */
int mosaic_item_for_each(mosaic_runtime *rt, u32 category,
                         int (*cb)(const mosaic_item_record *item, void *user), void *user) {
  if (!rt || !cb) return -1;
  for (size_t i = 0; i < rt->n_packs; i++) {
    const u8 *map = rt->packs[i].map;
    u64 n = hdr_item_count(map);
    if (!n) continue;
    u64 lo, hi;
    item_cat_range(map, n, category, &lo, &hi);
    const mosaic_item_record *items = (const mosaic_item_record *)(map + hdr_item_off(map));
    for (u64 k = lo; k < hi; k++) {
      int r = cb(&items[k], user);
      if (r) return r;
    }
  }
  return 0;
}

/* 描述符字段字符串读取:item 记录可能来自任意 pack,用 uintptr_t 扫描 pack
   范围(base ≤ ptr < base+len)定位(与 mosaic_runtime_module_string 同款
   模式;implementation-defined,依赖 Linux 用户空间映射不重叠,其他平台
   可能返回 NULL)。off == 0(无)或越界 → NULL。 */
static const char *item_string(const mosaic_runtime *rt, const mosaic_item_record *item, u32 off) {
  if (!rt || !item || off == 0) return NULL;
  uintptr_t p = (uintptr_t)item;
  for (size_t i = 0; i < rt->n_packs; i++) {
    uintptr_t b = (uintptr_t)rt->packs[i].map;
    if (p >= b && p < b + rt->packs[i].map_len) {
      const u8 *map = rt->packs[i].map;
      u64 base = hdr_meta_off(map);
      if (base + off >= rt->packs[i].map_len) return NULL;
      return (const char *)(map + base + off);
    }
  }
  return NULL;
}

const char *mosaic_item_name(mosaic_runtime *rt, const mosaic_item_record *item) {
  if (!item) return NULL;
  return item_string(rt, item, mi_name_off(item));
}
const char *mosaic_item_tags(mosaic_runtime *rt, const mosaic_item_record *item) {
  if (!item) return NULL;
  return item_string(rt, item, mi_tags_off(item));
}
const char *mosaic_item_icon(mosaic_runtime *rt, const mosaic_item_record *item) {
  if (!item) return NULL;
  return item_string(rt, item, mi_icon_off(item));
}
