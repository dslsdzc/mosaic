#include "mosaic_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_err(mosaic_runtime *rt, u32 code, char *errbuf, size_t errlen, const char *msg) {
  rt->last_err = code;
  if (errbuf && errlen) snprintf(errbuf, errlen, "%s", msg);
}

/* 校验并换算各表在 map 内的位置;失败返回 -1 并 set_err。
   M1.5-A:map/map_len 改为入参(open_many 逐 pack 校验)。
   M2-2b:tx_begin 复用(补丁 pack 独立 mmap,同款校验)。 */
int validate_layout(mosaic_runtime *rt, const u8 *map, size_t map_len,
                    char *errbuf, size_t errlen) {
  const u8 *h = map;
  if (hdr_magic(h) != MOSAIC_PACK_MAGIC) { set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "bad magic"); return -1; }
  if (hdr_version(h) != MOSAIC_PACK_VERSION) { set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "bad version"); return -1; }
  u64 moff = hdr_module_off(h), mc = hdr_module_count(h);
  u64 foff = hdr_fn_off(h), fc = hdr_fn_count(h);
  u64 toff = hdr_trigger_off(h), tc = hdr_trigger_count(h);
  u64 doff = hdr_dep_off(h), dc = hdr_dep_count(h);
  u64 soff = hdr_state_off(h), scap = hdr_state_cap(h), slen = hdr_state_len(h);
  u64 meoff = hdr_meta_off(h), melen = hdr_meta_len(h);
  u64 eoff = hdr_event_names_off(h), ec = hdr_event_count(h);
  u64 itoff = hdr_item_off(h), ic = hdr_item_count(h);
  /* 每表:偏移本身必须 ≤ map_len;count×size 用除法防 u64 回绕 */
  if (moff > map_len || mc > (map_len - moff) / MM_SIZE ||
      foff > map_len || fc > (map_len - foff) / FN_SIZE ||
      toff > map_len || tc > (map_len - toff) / MT_SIZE ||
      doff > map_len || dc > (map_len - doff) / MD_SIZE ||
      meoff > map_len || melen > map_len - meoff ||
      eoff > map_len || ec > (map_len - eoff) / MN_SIZE ||
      /* M2-4(v3):item 表边界(off + count*IT_SIZE ≤ map_len) */
      itoff > map_len || ic > (map_len - itoff) / IT_SIZE ||
      soff > map_len || scap > map_len - soff || slen > scap) {
    set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "offset out of bounds");
    return -1;
  }
  return 0;
}

/* M1.5-A 排序键:(min, max)。空范围 (1, 0) 排最前;同 min 时空范围(0)先于真实范围。
   min 相同的真实范围不可能出现——open_many 拒绝重叠,同 min 即重叠 */
static int cmp_pack_view(const void *a, const void *b_) {
  const struct pack_view *x = a, *y = b_;
  if (x->min_mod != y->min_mod) return x->min_mod < y->min_mod ? -1 : 1;
  if (x->max_mod != y->max_mod) return x->max_mod < y->max_mod ? -1 : 1;
  return 0;
}

/* 事件表逐条一致性(事件是宇宙级全局命名空间,dispatch 的 event_id 跨 pack 一致):
   count、名字、顺序全部与 pack 0 相同;≤64 条,直接比较名字串
   M2-2b:tx_begin 复用(base pack 0 vs 补丁 pack)。 */
int event_tables_match(const struct pack_view *a, const struct pack_view *b) {
  const u8 *m0 = a->map, *m1 = b->map;
  u32 e0 = hdr_event_count(m0), e1 = hdr_event_count(m1);
  if (e0 != e1) return 0;
  if (e0 == 0) return 1;
  const mosaic_event_name *n0 = (const mosaic_event_name *)(m0 + hdr_event_names_off(m0));
  const mosaic_event_name *n1 = (const mosaic_event_name *)(m1 + hdr_event_names_off(m1));
  u64 meta0 = hdr_meta_off(m0), meta1 = hdr_meta_off(m1);
  for (u32 k = 0; k < e0; k++) {
    u32 l0 = mn_len(&n0[k]), l1 = mn_len(&n1[k]);
    u32 o0 = mn_off(&n0[k]), o1 = mn_off(&n1[k]);
    if (l0 != l1) return 0;
    if (meta0 + o0 + l0 > a->map_len || meta1 + o1 + l1 > b->map_len) return 0;
    if (memcmp(m0 + meta0 + o0, m1 + meta1 + o1, l0) != 0) return 0;
  }
  return 1;
}

mosaic_runtime *mosaic_runtime_open_many(const char *const *paths, size_t n_packs,
                                         char *errbuf, size_t errlen) {
  if (n_packs == 0 || !paths) {
    if (errbuf && errlen) snprintf(errbuf, errlen, "no packs");
    return NULL;
  }
  struct pack_view *packs = calloc(n_packs, sizeof *packs);
  if (!packs) { if (errbuf && errlen) snprintf(errbuf, errlen, "oom"); return NULL; }
  mosaic_runtime *rt = calloc(1, sizeof *rt);
  if (!rt) { free(packs); if (errbuf && errlen) snprintf(errbuf, errlen, "oom"); return NULL; }
  rt->packs = packs;
  rt->n_packs = n_packs;
  rt->last_err = MOSAIC_OK;
  /* I-1:calloc 清零使未处理条目的 fd == 0,失败清理循环会误 close(0)(关掉
     stdin)。打开循环前把所有条目预置为无效 fd。 */
  for (size_t i = 0; i < n_packs; i++) rt->packs[i].fd = -1;
  for (size_t i = 0; i < n_packs; i++) {
    /* 修正 D-10-3(逐 pack):state_blob_append 需要 ftruncate 扩容文件再 mremap
       (否则写入越过文件末页 → SIGBUS);先试 O_RDWR,只读 pack 回退 O_RDONLY
       (此时有状态写入的墓碑会走 NOMEM 错误路径优雅失败,纯查询不受影响)。 */
    struct pack_view *pv = &packs[i];
    pv->fd = -1;
    int fd = open(paths[i], O_RDWR);
    if (fd < 0) fd = open(paths[i], O_RDONLY);
    if (fd < 0) { if (errbuf && errlen) snprintf(errbuf, errlen, "open %s failed", paths[i]); goto fail; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < HDR_SIZE) {
      close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "pack too small"); goto fail;
    }
    size_t len = (size_t)st.st_size;
    void *map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "mmap failed"); goto fail; }
    pv->fd = fd; pv->map = map; pv->map_len = len;
    pv->state_len = hdr_state_len(map);
    pv->min_mod = 1; pv->max_mod = 0;   /* 空模块表默认空范围 */
  }
  for (size_t i = 0; i < n_packs; i++)
    if (validate_layout(rt, packs[i].map, packs[i].map_len, errbuf, errlen) != 0) goto fail;
  /* 模块范围表:模块表按 id 排序,取首/末条;空表 → 空范围(跳过) */
  for (size_t i = 0; i < n_packs; i++) {
    u64 mc = hdr_module_count(packs[i].map);
    if (mc) {
      const mosaic_module_record *mods = (const mosaic_module_record *)(packs[i].map + hdr_module_off(packs[i].map));
      packs[i].min_mod = mm_id(&mods[0]);
      packs[i].max_mod = mm_id(&mods[mc - 1]);
    }
  }
  /* 排序(按 min),rt->packs 顺序即范围表顺序 */
  qsort(packs, n_packs, sizeof *packs, cmp_pack_view);
  /* 互不重叠:fn_id 空间要求 module_id 全局唯一。排序后检查;空范围 (1,0)
     的 max < min,直接跳过——它既不能与相邻 pack 误报重叠(含 module_id=0
     的 pack 相邻时 min=0 的双向条件会误报),也不能阻断检查:空 pack 夹在
     两个重叠的非空 pack 之间时(如 A(0,5)、空、B(3,8)),只比较相邻两两
     会漏掉 A∩B。因此跟踪上一个非空 pack,跳过空范围仍与最近的非空 pack
     比较(M1.5-A 复评)。 */
  size_t prev = (size_t)-1;
  for (size_t i = 0; i < n_packs; i++) {
    if (packs[i].max_mod < packs[i].min_mod) continue;   /* 空范围,跳过 */
    if (prev != (size_t)-1 &&
        packs[prev].min_mod <= packs[i].max_mod &&
        packs[i].min_mod <= packs[prev].max_mod) {
      if (errbuf && errlen) snprintf(errbuf, errlen, "overlapping pack module ranges");
      goto fail;
    }
    prev = i;
  }
  /* 事件表一致性:所有 pack 与 packs[0] 逐条相同 */
  for (size_t i = 1; i < n_packs; i++) {
    if (!event_tables_match(&packs[0], &packs[i])) {
      if (errbuf && errlen) snprintf(errbuf, errlen, "event table mismatch");
      goto fail;
    }
  }
  return rt;
fail:
  for (size_t i = 0; i < n_packs; i++) {
    if (packs[i].map) munmap(packs[i].map, packs[i].map_len);
    if (packs[i].fd >= 0) close(packs[i].fd);
  }
  free(packs);
  free(rt);
  return NULL;
}

mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen) {
  const char *paths[1] = { pack_path };
  return mosaic_runtime_open_many(paths, 1, errbuf, errlen);
}

void mosaic_runtime_close(mosaic_runtime *rt) {
  if (!rt) return;
  if (rt->mods.cap) {
    for (u64 i = 0; i < rt->mods.cap; i++) {
      struct mod_entry *m = rt->mods.vals[i];
      if (m) { if (m->so) dlclose(m->so); free(m); }
    }
    free(rt->mods.keys); free(rt->mods.vals);
  }
  /* M2-2b 修复(I-1):commit 失效条目链(旧 .so 延迟释放),close 一并收尾 */
  for (struct mod_entry *m = rt->mods_dead; m; ) {
    struct mod_entry *nx = m->next;
    if (m->so) dlclose(m->so);
    free(m);
    m = nx;
  }
  /* M2-3 修(ASan):state 是 arena_alloc 的独立 malloc,close 须先释放每个
     活动 fn 的 state(否则并行物化 → ASan 报泄漏)。必须在 slab 释放之前做:
     fn_obj 本体在 slab 里。墓碑路径先 ws_remove 再 free(state),因此 ws 中
     存活的 fn 其 state 必未释放,本循环无双重释放。 */
  for (u64 i = 0; i < rt->ws.cap; i++)
    if (rt->ws.vals[i] && rt->ws.vals[i]->state) { free(rt->ws.vals[i]->state); rt->ws.vals[i]->state = NULL; }
  for (struct slab *s = rt->slabs; s; ) { struct slab *nx = s->next; free(s->start); free(s); s = nx; }
  free(rt->ws.keys); free(rt->ws.vals);
  /* M2-2a:generation 路由表(NULL = 无更新);gen_route_free 释放内部数组,
     表体本身(M2-2b 由 tx 分配)在此一并释放 */
  if (rt->routes) { gen_route_free(rt->routes); free(rt->routes); }
  /* M2-2b:已 commit 补丁 pack(未 rollback 的残留,如 close 前未 demote) */
  for (size_t i = 0; i < rt->n_tx_packs; i++) {
    munmap(rt->tx_packs[i].map, rt->tx_packs[i].map_len);
    close(rt->tx_packs[i].fd);
  }
  free(rt->tx_packs);
  for (size_t i = 0; i < rt->n_packs; i++) {
    munmap(rt->packs[i].map, rt->packs[i].map_len);
    close(rt->packs[i].fd);
  }
  free(rt->packs);
  free(rt);
}

u32 mosaic_runtime_last_error(const mosaic_runtime *rt) { return rt ? rt->last_err : MOSAIC_ERR_IO; }

/* M3-3:工作集大小 = ws 哈希中已物化 ACTIVE 函数数(驱逐/调优观测点) */
u32 mosaic_runtime_working_set_count(const mosaic_runtime *rt) {
  return rt ? (u32)rt->ws.len : 0;
}

u64 mosaic_runtime_function_count(const mosaic_runtime *rt) {
  if (!rt) return 0;
  u64 total = 0;
  for (size_t i = 0; i < rt->n_packs; i++) total += hdr_fn_count(rt->packs[i].map);
  return total;
}

/* M1.5-A:ex 变体带 pack 参数(生命周期内部用,不依赖指针扫描)。
   M2-2b:pack 下标经 pack_view 解析(基础 pack 与 tx_packs 统一)——补丁模块
   mod_load 读 so_path 时 pack = n_packs + i,落在补丁 pack。 */
const char *module_string_ex(const mosaic_runtime *rt, size_t pack, const mosaic_module_record *m, u32 off) {
  if (!rt || !m || off == 0) return NULL;
  const struct pack_view *pv = pack_view((mosaic_runtime *)rt, pack);
  if (!pv) return NULL;
  const u8 *map = pv->map;
  u64 base = hdr_meta_off(map);
  if (base + off >= pv->map_len) return NULL;
  return (const char *)(map + base + off);
}

/* 公开接口:off 语义指向 meta blob;m 的归属 pack 未知,用 uintptr_t 扫描
   pack 范围(base ≤ ptr < base+len)定位。implementation-defined:依赖 Linux
   用户空间活动映射互不重叠、指针值全序(其他平台可能返回 NULL)。 */
const char *mosaic_runtime_module_string(const mosaic_runtime *rt, const mosaic_module_record *m, u32 off) {
  if (!rt || !m || off == 0) return NULL;
  uintptr_t p = (uintptr_t)m;
  for (size_t i = 0; i < rt->n_packs; i++) {
    uintptr_t b = (uintptr_t)rt->packs[i].map;
    if (p >= b && p < b + rt->packs[i].map_len)
      return module_string_ex(rt, i, m, off);
  }
  return NULL;
}

/* M1.5-A:事件查找在 pack 0 的事件表二分(所有 pack 已校验一致) */
u32 mosaic_runtime_event_id(mosaic_runtime *rt, const char *name) {
  if (!rt || !name) return MOSAIC_U32_NONE;
  const u8 *map = pack_map(rt, 0);
  if (!map) { rt->last_err = MOSAIC_ERR_BAD_PACK; return MOSAIC_U32_NONE; }
  u64 ec = hdr_event_count(map);
  u64 eoff = hdr_event_names_off(map);
  u64 base = hdr_meta_off(map);
  size_t nl = strlen(name);
  /* 二分查找:event_names 表在构建期按名排序(v2),事件 id = 排序位置。
     比较用显式长度 + memcmp 前缀,长度不同天然分序——前缀/截断串不会误匹配 */
  u64 lo = 0, hi = ec;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    const mosaic_event_name *en = (const mosaic_event_name *)(map + eoff + mid * MN_SIZE);
    u32 o = mn_off(en), l = mn_len(en);
    if (base + o + l > rt->packs[0].map_len) { rt->last_err = MOSAIC_ERR_BAD_PACK; return MOSAIC_U32_NONE; }
    const char *p = (const char *)(map + base + o);
    size_t c = l < nl ? (size_t)l : nl;
    int r = memcmp(name, p, c);
    if (r == 0) r = (nl > (size_t)l) - (nl < (size_t)l);
    if (r == 0) return (u32)mid;                 /* 名字匹配 → id = 排序位置 */
    if (r < 0) hi = mid; else lo = mid + 1;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;   /* 未命中写错误槽(与 find_module/find_function 同惯例) */
  return MOSAIC_U32_NONE;
}
