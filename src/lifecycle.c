/* src/lifecycle.c — Task 6/7 实现:模块 ABI 加载 + 物化/恢复路径 + 热路径/墓碑 */
#define _GNU_SOURCE   /* mremap/MREMAP_MAYMOVE 需要 */
#include "mosaic_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>   /* ftruncate(D-10-3:文件映射扩容先撑文件) */

/* ---- mods 哈希(评审已知缺陷修复):单链表全链扫描 → 开放寻址哈希。
   容量恒为 2 的幂,& (cap-1) 取模;0 = 空槽(module_id ≥ 1,哨兵安全)。 ---- */
static struct mod_entry *mods_get(mosaic_runtime *rt, u64 module_id) {
  struct mods_hash *h = &rt->mods;
  if (!h->cap) return NULL;
  u64 mask = h->cap - 1;
  u64 i = module_id & mask;
  for (u64 n = 0; n < h->cap; n++) {
    u64 k = h->keys[i];
    if (!k) return NULL;
    if (k == module_id) return h->vals[i];
    i = (i + 1) & mask;
  }
  return NULL;
}

static int mods_grow(mosaic_runtime *rt) {
  struct mods_hash *h = &rt->mods;
  u64 cap = h->cap ? h->cap * 2 : 16;
  u64 *keys = calloc(cap, sizeof *keys);
  struct mod_entry **vals = calloc(cap, sizeof *vals);
  if (!keys || !vals) { free(keys); free(vals); rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
  for (u64 i = 0; i < h->cap; i++) {
    u64 k = h->keys[i];
    if (!k) continue;
    u64 j = k & (cap - 1);
    while (keys[j]) j = (j + 1) & (cap - 1);
    keys[j] = k; vals[j] = h->vals[i];
  }
  free(h->keys); free(h->vals);
  h->keys = keys; h->vals = vals; h->cap = cap;
  return 0;
}

/* 负载 ≥70% 扩容 ×2;失败返回 -1 并设 NOMEM(调用方回滚 dlclose+free) */
static int mods_put(mosaic_runtime *rt, struct mod_entry *m) {
  struct mods_hash *h = &rt->mods;
  if (h->len * 10 >= h->cap * 7) {
    if (mods_grow(rt) != 0) return -1;
  }
  u64 mask = h->cap - 1;
  u64 i = m->module_id & mask;
  while (h->keys[i]) i = (i + 1) & mask;
  h->keys[i] = m->module_id; h->vals[i] = m;
  h->len++;
  return 0;
}

/* ---- 延迟 dlclose(缺陷 2 修复)----
   关键语义:哈希内条目只在 flush/close 时移除;refs 归零的条目保留为 pending
   (.so 不卸载——自墓碑时正在执行的 .so 若被 dlclose,代码页被 unmap,返回
   地址悬垂 → 返回即崩),因此 mods 管理没有逐条删除路径(无后移逻辑)。 */

/* flush 后重建:新表容量 = 存活数 × 2(向上取 2 的幂,线性探测需要),重插活
   条目,释放旧表。用重建替代开放寻址删除的后移逻辑——重建 O(n) 且 flush
   罕见(只在 dispatch/evict 末尾);OOM 时保留旧表(空洞表仍可用) */
static void mods_compact(mosaic_runtime *rt) {
  struct mods_hash *h = &rt->mods;
  u64 alive = 0;
  if (h->cap)
    for (u64 i = 0; i < h->cap; i++)
      if (h->vals[i]) alive++;
  if (alive == 0) {
    free(h->keys); free(h->vals);
    h->keys = NULL; h->vals = NULL;
    h->cap = h->len = 0;
    return;
  }
  u64 cap = 1;
  while (cap < alive * 2) cap <<= 1;
  u64 *keys = calloc(cap, sizeof *keys);
  struct mod_entry **vals = calloc(cap, sizeof *vals);
  if (!keys || !vals) { free(keys); free(vals); return; }   /* OOM:保留旧表 */
  u64 mask = cap - 1;
  for (u64 i = 0; i < h->cap; i++) {
    struct mod_entry *m = h->vals[i];
    if (!m) continue;
    u64 j = m->module_id & mask;
    while (keys[j]) j = (j + 1) & mask;
    keys[j] = m->module_id; vals[j] = m;
  }
  free(h->keys); free(h->vals);
  h->keys = keys; h->vals = vals; h->cap = cap;
  h->len = alive;
}

void flush_pending_dlclose(mosaic_runtime *rt) {
  struct mods_hash *h = &rt->mods;
  if (!h->cap) return;
  for (u64 i = 0; i < h->cap; i++) {
    struct mod_entry *m = h->vals[i];
    if (!m) continue;
    if (m->pending) {
      if (m->so) dlclose(m->so);
      free(m);
      h->keys[i] = 0; h->vals[i] = NULL;
    }
  }
  mods_compact(rt);
}

const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id) {
  struct mod_entry *m = mods_get(rt, module_id);
  if (m) {
    if (m->pending) { m->pending = 0; m->refs = 1; return m->abi; }   /* 复活,不重新 dlopen */
    m->refs++;
    return m->abi;
  }
  /* M1.5-A:find_module_ex 给出归属 pack,module_string_ex 直接读该 pack 的 meta
     (公开 module_string 的 uintptr_t 指针扫描是 implementation-defined,生命周期
     内部不依赖它)。
     M2-2b:find_module_active——已 commit 补丁的模块记录优先(补丁模块的
     so_path/version 以补丁为准,更新后可能指向新 .so) */
  size_t pack = 0;
  const mosaic_module_record *rec = find_module_active(rt, module_id, &pack);
  if (!rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const char *path = module_string_ex(rt, pack, rec, mm_so_off(rec));
  if (!path) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  void *so = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
  if (!so) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  dlerror();
  mosaic_module_abi_v1_fn sym = (mosaic_module_abi_v1_fn)dlsym(so, "mosaic_module_abi_v1");
  const char *e = dlerror();
  if (e) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  const mosaic_module_abi *abi = sym();
  /* dlsym 返回 NULL 且 dlerror()==NULL(符号存在但函数返回 NULL)时,上面的
     e 检查不会命中,此防护补这个洞:abi 为 NULL 时不得解引用 */
  if (!abi) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  if (abi->abi_version != MOSAIC_MODULE_ABI_VERSION) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  /* 偏差 D-5 + 修正 D-10-1:计划原文用 == 校验,Task 6 改 <(导出槽位数少于声明
     函数数 → 必有一函数越界 → 拒绝)。但代码表是 code_off 槽位表,Task 10 合成
     宇宙设计明确"10M 函数共享 3 个代码入口"(模块 10 函数/冷包 1000 函数,code_off
     均 ∈ [0,3)),声明函数数可以大于导出槽位数;< 校验会误杀合法共享槽位 pack。
     故移除该急切校验,越界一律由 materialize 的 co >= fn_count 惰性校验兜底
     (test_lifecycle test_tombstone_illegal_transitions code_off=5 仍被拒)。 */
  (void)rec;
  m = calloc(1, sizeof *m);
  if (!m) { dlclose(so); rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
  m->module_id = module_id; m->so = so; m->abi = abi; m->refs = 1;
  if (mods_put(rt, m) != 0) { dlclose(so); free(m); return NULL; }   /* OOM 回滚 */
  return abi;
}

void mod_unload(mosaic_runtime *rt, u64 module_id) {
  struct mod_entry *m = mods_get(rt, module_id);
  if (!m) return;
  if (m->refs > 1) { m->refs--; return; }
  /* refs 归零 → pending = 1,不 dlclose(可能正被自身代码执行中);.so 保持
     加载,由下一个安全点(dispatch/evict 末尾)flush_pending_dlclose 卸载 */
  m->refs = 0;
  m->pending = 1;
}

/* 状态 blob 布局:[4B reserved][(u32 len, bytes) 条目...]。
   偏差 D-2:计划原文首条目偏移返回 0,与 TOMBSTONED = COLD + state_off≠0
   (恢复判定 soff!=0、测试 MT_CHECK(mf_state_off(rec) != 0))矛盾;
   故 blob 头部保留 4B 前缀,条目偏移恒 ≥ 4。materialize 恢复代码保持原文不变。
   M1.5-A:pack 参数——只对该 pack 的 map mremap(其他 pack 的指针不受影响,
   这是分片架构的额外好处);state_len 游标也随 pack 走(struct pack_view)。 */
/* M2-2b:核心实现按 pack_view 寻址(不假设 rt->packs)——补丁 pack 在 commit
   前只由 tx 持有(不在 rt->tx_packs),state_blob_append_pack(rt, &tx->patch, ...)
   直接写补丁 blob;转持后的补丁记录墓碑经 state_blob_append(pack = n_packs+i)
   同样落到本实现(pack_view 解析)。 */
int state_blob_append_pack(mosaic_runtime *rt, struct pack_view *pv, const void *bytes, u32 len, u32 *out_off) {
  const u8 *h = pv->map;
  u64 base = hdr_state_off(h);
  u64 cap = hdr_state_cap(h);
  u64 used = pv->state_len;
  u64 pos = used == 0 ? 4 : used;
  if (pos + 4ull + len > cap) {
    /* 扩容:mremap 翻倍(修正 D-10-3:文件映射越过 EOF 的页写入会 SIGBUS,
       必须先 ftruncate 撑大文件,再 mremap 扩 VMA;只读 fd 上 ftruncate 失败
       走 NOMEM 优雅降级。mremap 仍可能整体搬家,调用方须在 append 后重取
       记录指针——I-1 修复已覆盖本函数自身,D-10-2 覆盖其他对象的缓存 rec) */
    u64 newcap = cap ? cap * 2 : 4096;
    while (newcap < pos + 4ull + len) newcap *= 2;
    size_t newlen = base + newcap;
    if (ftruncate(pv->fd, (off_t)newlen) != 0) { rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
    void *np = mremap(pv->map, pv->map_len, newlen, MREMAP_MAYMOVE);
    if (np == MAP_FAILED) { rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
    pv->map = np; pv->map_len = newlen;
    hdr_set_state_cap(pv->map, newcap);
  }
  u8 *dst = pv->map + base + pos;
  wr_le32(dst, len);
  memcpy(dst + 4, bytes, len);
  pv->state_len = pos + 4ull + len;
  hdr_set_state_len(pv->map, pv->state_len);
  *out_off = (u32)pos;
  return 0;
}

/* pack 下标 → pack_view(基础 pack 与 tx_packs 统一,补丁记录经此写补丁 blob) */
int state_blob_append(mosaic_runtime *rt, size_t pack, const void *bytes, u32 len, u32 *out_off) {
  struct pack_view *pv = pack_view(rt, pack);
  if (!pv) { if (rt) rt->last_err = MOSAIC_ERR_BAD_PACK; return -1; }
  return state_blob_append_pack(rt, pv, bytes, len, out_off);
}

mosaic_fn_obj *mosaic_fn_materialize(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  /* 偏差 D-4a(Task 5 评审 ⚠️-1):fn_id==0 与 ws 哈希空槽哨兵冲突,禁止注入 */
  if (fn_id == 0) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  size_t pack = 0;
  /* M2-2b:活跃代记录解析——gen_route 命中的 fn 走补丁 pack 记录,未命中走
     基础 pack;out_pack 同时给出 blob 归属 pack(补丁记录 → tx_packs)。 */
  const mosaic_function_record *rec = find_function_active(rt, fn_id, &pack);
  if (!rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  mosaic_fn_obj *ex = ws_find(rt, fn_id);
  if (ex) return ex;   /* 已 ACTIVE:直接返回(幂等) */
  u16 fl = mf_flags(rec);
  u8 st = (u8)(fl & MOSAIC_FN_STATE_MASK);
  if (st != MOSAIC_FN_STATE_COLD) { rt->last_err = MOSAIC_ERR_ILLEGAL; return NULL; }
  mosaic_function_record *rw = (mosaic_function_record *)rec;   /* MAP_PRIVATE 可写 */
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_MATERIALIZING));

  const mosaic_module_abi *abi = mod_load(rt, mf_module_id(rec));
  if (!abi) { mf_set_flags(rw, fl); return NULL; }
  u32 co = mf_code_off(rec);
  if (co >= abi->fn_count) { rt->last_err = MOSAIC_ERR_ABI; mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }

  void *state = NULL;
  u32 sz = mf_state_size(rec);
  if (fl & MOSAIC_FN_REQUIRES_STATE) {
    if (sz == 0) sz = abi->state_size;
    void *p = NULL; arena_zalloc(rt, sz, &p);
    if (!p) { mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }
    state = p;
    u32 soff = mf_state_off(rec);
    if (soff != 0) {
      /* TOMBSTONED → RESTORE:从记录归属 pack 的 state blob 读回(基础 pack 或
         tx_packs,pack_view 统一解析;补丁记录的 v2 状态在补丁 blob) */
      struct pack_view *pv = pack_view(rt, pack);
      if (pv) {
        const u8 *h = pv->map;
        u64 base = hdr_state_off(h);
        if (base + soff + 4 <= pv->map_len) {
          u32 len = rd_le32(pv->map + base + soff);
          if (len <= sz && base + soff + 4 + len <= pv->map_len)
            memcpy(state, pv->map + base + soff + 4, len);
        }
      }
    }
  }

  mosaic_fn_obj *fn = fn_alloc(rt);
  if (!fn) { free(state); mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }
  fn->fn_id = fn_id;
  fn->pack = (u32)pack;   /* M1.5-A:归属 pack,墓碑时定位 state blob */
  fn->rec = rec;
  fn->code = abi->fns[co].code;
  fn->state = state;
  fn->state_size = sz;
  fn->last_use = now_ns();
  ws_insert(rt, fn);
  /* 偏差 D-4b(Task 5 评审 I-1):ws_insert 在 ws_grow OOM 时静默跳过,
     若不检查则出现 flags=ACTIVE 但对象不在工作集的非法状态 → 回滚 */
  if (ws_find(rt, fn_id) != fn) {
    free(state); fn_free(rt, fn); mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec));
    rt->last_err = MOSAIC_ERR_NOMEM;
    return NULL;
  }
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_ACTIVE));
  return fn;
}

/* 偏差 D-1:Task 6 测试(test_lifecycle.c)依赖 execute/tombstone,但计划把二者
   分派给 Task 7;此处按计划 Task 7 原文提前落地,Task 7 将仅追加测试。
   D-10-4:execute 移入 function.h 为 static inline(热路径零开销),定义见头文件。 */

int mosaic_fn_tombstone(mosaic_runtime *rt, mosaic_fn_obj *fn) {
  if (!rt || !fn) { if (rt) rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  /* 修正 D-10-2:state_blob_append 的 mremap(MREMAP_MAYMOVE)可能移动 pack 映射,
     使其他 fn 对象缓存的 rec 指针悬垂(Task 10 S2 冷包 1000 函数逐批墓碑
     实测 SIGSEGV——I-1 修复只重取了本函数自己的 rw,未保护其他对象的 rec)。
     与 I-1 同一模式:入口按 fn_id 重取记录(经 find_function_active 同时刷新
     fn->pack 指向当前归属 pack——v1/v2 都以活跃代解析为准),后续全部使用
     新鲜指针。 */
  size_t pack = 0;
  /* M2-2b:活跃代解析——v2 fn 的墓碑走补丁记录、v1 fn 走基础记录(commit 的
     quiesce 在路由切换前执行,因此墓碑 v1 对象时仍解析到基础记录) */
  fn->rec = find_function_active(rt, fn->fn_id, &pack);
  if (!fn->rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return -1; }
  fn->pack = (u32)pack;
  u16 fl = mf_flags(fn->rec);
  u8 st = (u8)(fl & MOSAIC_FN_STATE_MASK);
  if (st != MOSAIC_FN_STATE_ACTIVE) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  if (fn->refs) { rt->last_err = MOSAIC_ERR_BUSY; return -1; }
  if (!(fl & MOSAIC_FN_TOMBSTONE_ABLE)) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  mosaic_function_record *rw = (mosaic_function_record *)fn->rec;
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_QUIESCING));
  if (fn->state && (fl & MOSAIC_FN_REQUIRES_STATE) && fn->state_size) {
    u32 off = 0;
    if (state_blob_append(rt, pack, fn->state, fn->state_size, &off) != 0) {
      /* 回滚为 ACTIVE;失败路径 mremap 未发生,但统一重取指针杜绝任何悬垂写 */
      rw = (mosaic_function_record *)find_function_active(rt, fn->fn_id, &pack);
      fn->pack = (u32)pack;
      mf_set_flags(rw, fl);
      return -1;
    }
    /* 修复 I-1(Task 6 评审):state_blob_append 内部可能 mremap(MREMAP_MAYMOVE)
       移动该 pack 映射,append 前缓存的 rw 在 append 后悬垂(评审已用相邻 VMA
       探针实证 SIGSEGV)。append 之后必须重取记录指针,并同步刷新 fn->rec 与
       fn->pack,供后续 mf_module_id(fn->rec) 与最终 set_flags(COLD) 使用。
       M2-2b:重取走活跃代解析(与入口一致)。 */
    rw = (mosaic_function_record *)find_function_active(rt, fn->fn_id, &pack);
    fn->rec = rw;
    fn->pack = (u32)pack;
    mf_set_state_off(rw, off);
  }
  /* 修复 M-1(Task 6 评审):fn_free 之后读 fn->rec 是逻辑 UAF(当前仅因结构体
     字段布局侥幸安全);module_id 必须在 fn_free 之前取出。 */
  u32 module_id = mf_module_id(fn->rec);
  ws_remove(rt, fn);
  free(fn->state);
  fn_free(rt, fn);
  mod_unload(rt, module_id);
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_COLD));
  return 0;
}
