/* src/lifecycle.c — Task 6/7 实现:模块 ABI 加载 + 物化/恢复路径 + 热路径/墓碑 */
#define _GNU_SOURCE   /* mremap/MREMAP_MAYMOVE 需要 */
#include "mosaic_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id) {
  for (struct mod_entry *m = rt->mods; m; m = m->next)
    if (m->module_id == module_id) { m->refs++; return m->abi; }
  const mosaic_module_record *rec = mosaic_runtime_find_module(rt, module_id);
  if (!rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const char *path = mosaic_runtime_module_string(rt, rec, mm_so_off(rec));
  if (!path) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  void *so = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
  if (!so) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  dlerror();
  mosaic_module_abi_v1_fn sym = (mosaic_module_abi_v1_fn)dlsym(so, "mosaic_module_abi_v1");
  const char *e = dlerror();
  if (e) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  const mosaic_module_abi *abi = sym();
  if (abi->abi_version != MOSAIC_MODULE_ABI_VERSION) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  /* 偏差 D-5:计划原文用 == 校验,但代码表是 code_off 槽位表,pack 可声明少量函数
     复用同一模块的少数槽位(计划自身测试:test_mod.so 3 槽配 1/2 函数 pack);
     == 会误杀合法 pack。改为:导出槽位数少于声明函数数 → 必有一函数越界 → 拒绝;
     单函数越界仍由 materialize 的 co >= fn_count 惰性校验兜底。 */
  if (abi->fn_count < mm_fn_count(rec)) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  struct mod_entry *m = calloc(1, sizeof *m);
  if (!m) { dlclose(so); rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
  m->module_id = module_id; m->so = so; m->abi = abi; m->refs = 1;
  m->next = rt->mods; rt->mods = m;
  return abi;
}

void mod_unload(mosaic_runtime *rt, u64 module_id) {
  for (struct mod_entry **pp = &rt->mods; *pp; pp = &(*pp)->next) {
    if ((*pp)->module_id == module_id) {
      struct mod_entry *m = *pp;
      if (m->refs > 1) { m->refs--; return; }
      *pp = m->next;
      dlclose(m->so);
      free(m);
      return;
    }
  }
}

/* 状态 blob 布局:[4B reserved][(u32 len, bytes) 条目...]。
   偏差 D-2:计划原文首条目偏移返回 0,与 TOMBSTONED = COLD + state_off≠0
   (恢复判定 soff!=0、测试 MT_CHECK(mf_state_off(rec) != 0))矛盾;
   故 blob 头部保留 4B 前缀,条目偏移恒 ≥ 4。materialize 恢复代码保持原文不变。 */
int state_blob_append(mosaic_runtime *rt, const void *bytes, u32 len, u32 *out_off) {
  const u8 *h = rt->map;
  u64 base = hdr_state_off(h);
  u64 cap = hdr_state_cap(h);
  u64 used = rt->state_len;
  u64 pos = used == 0 ? 4 : used;
  if (pos + 4ull + len > cap) {
    /* 扩容:mremap 翻倍 */
    u64 newcap = cap ? cap * 2 : 4096;
    while (newcap < pos + 4ull + len) newcap *= 2;
    size_t newlen = base + newcap;
    void *np = mremap(rt->map, rt->map_len, newlen, MREMAP_MAYMOVE);
    if (np == MAP_FAILED) { rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
    rt->map = np; rt->map_len = newlen;
    hdr_set_state_cap(rt->map, newcap);
  }
  u8 *dst = rt->map + base + pos;
  wr_le32(dst, len);
  memcpy(dst + 4, bytes, len);
  rt->state_len = pos + 4ull + len;
  hdr_set_state_len(rt->map, rt->state_len);
  *out_off = (u32)pos;
  return 0;
}

mosaic_fn_obj *mosaic_fn_materialize(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  /* 偏差 D-4a(Task 5 评审 ⚠️-1):fn_id==0 与 ws 哈希空槽哨兵冲突,禁止注入 */
  if (fn_id == 0) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, fn_id);
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
      /* TOMBSTONED → RESTORE:从 state blob 读回 */
      const u8 *h = rt->map;
      u64 base = hdr_state_off(h);
      if (base + soff + 4 <= rt->map_len) {
        u32 len = rd_le32(rt->map + base + soff);
        if (len <= sz && base + soff + 4 + len <= rt->map_len)
          memcpy(state, rt->map + base + soff + 4, len);
      }
    }
  }

  mosaic_fn_obj *fn = fn_alloc(rt);
  if (!fn) { free(state); mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }
  fn->fn_id = fn_id;
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
   分派给 Task 7;此处按计划 Task 7 原文提前落地,Task 7 将仅追加测试。 */

void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event) {
  /* 热路径:零检查,直接调用 */
  fn->code(fn->state, event_id, event);
}

int mosaic_fn_tombstone(mosaic_runtime *rt, mosaic_fn_obj *fn) {
  if (!rt || !fn) { if (rt) rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  u16 fl = mf_flags(fn->rec);
  u8 st = (u8)(fl & MOSAIC_FN_STATE_MASK);
  if (st != MOSAIC_FN_STATE_ACTIVE) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  if (fn->refs) { rt->last_err = MOSAIC_ERR_BUSY; return -1; }
  if (!(fl & MOSAIC_FN_TOMBSTONE_ABLE)) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  mosaic_function_record *rw = (mosaic_function_record *)fn->rec;
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_QUIESCING));
  if (fn->state && (fl & MOSAIC_FN_REQUIRES_STATE) && fn->state_size) {
    u32 off = 0;
    if (state_blob_append(rt, fn->state, fn->state_size, &off) != 0) {
      /* 回滚为 ACTIVE;失败路径 mremap 未发生,但统一重取指针杜绝任何悬垂写 */
      rw = (mosaic_function_record *)mosaic_runtime_find_function(rt, fn->fn_id);
      mf_set_flags(rw, fl);
      return -1;
    }
    /* 修复 I-1(Task 6 评审):state_blob_append 内部可能 mremap(MREMAP_MAYMOVE)
       移动整个 pack 映射,append 前缓存的 rw 在 append 后悬垂(评审已用相邻 VMA
       探针实证 SIGSEGV)。append 之后必须重取记录指针,并同步刷新 fn->rec,
       供后续 mf_module_id(fn->rec) 与最终 set_flags(COLD) 使用。 */
    rw = (mosaic_function_record *)mosaic_runtime_find_function(rt, fn->fn_id);
    fn->rec = rw;
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
