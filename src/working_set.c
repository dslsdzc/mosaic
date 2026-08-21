/* src/working_set.c — Task 5 实现 */
#include "mosaic_internal.h"
#include <stdlib.h>
#include <string.h>

#define SLAB_SIZE 65536

static struct slab *slab_new(mosaic_runtime *rt) {
  struct slab *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->start = malloc(SLAB_SIZE);
  if (!s->start) { free(s); return NULL; }
  s->end = s->start + SLAB_SIZE;
  s->cur = s->start;
  s->next = rt->slabs;
  rt->slabs = s;
  return s;
}

struct mosaic_fn_obj *fn_alloc(mosaic_runtime *rt) {
  struct slab *s = NULL;
  struct mosaic_fn_obj *f = NULL;
  /* 优先 slab 空闲链表(链在 fn->next 上) */
  for (struct slab *it = rt->slabs; it; it = it->next)
    if (it->free_head) { s = it; f = it->free_head; it->free_head = (struct mosaic_fn_obj *)f->next; break; }
  if (!f) {
    s = rt->slabs;
    if (!s || s->cur + sizeof(struct mosaic_fn_obj) > s->end) {
      s = slab_new(rt);
      if (!s) { rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
    }
    f = (struct mosaic_fn_obj *)s->cur;
    s->cur += sizeof(struct mosaic_fn_obj);
  }
  memset(f, 0, sizeof *f);   /* 两条路径都清零,保证 freq/refs 等字段确定 */
  f->slab = s;
  return f;
}

void fn_free(mosaic_runtime *rt, struct mosaic_fn_obj *fn) {
  (void)rt;
  if (!fn) return;
  struct slab *s = fn->slab;
  if (s) { fn->next = (struct mosaic_fn_obj *)s->free_head; s->free_head = fn; }
  else free(fn);
}

void *arena_alloc(mosaic_runtime *rt, size_t n) {
  /* 变量大小 state 暂用 malloc;固定大小对象走 slab(M1 简化,文档已注明) */
  (void)rt;
  void *p = malloc(n);
  if (!p) rt->last_err = MOSAIC_ERR_NOMEM;
  return p;
}

void arena_zalloc(mosaic_runtime *rt, size_t n, void **out) {
  void *p = arena_alloc(rt, n);
  if (p) memset(p, 0, n);
  *out = p;
}

/* 哈希键混合:fn_id 编码为 (module<<32)|local,低 32 位几乎恒定(module 从
   bit 32 起),直接 `fn_id & mask` 会把全部条目散列到 ~10 个槽 → 簇内链线性
   增长,查找退化 O(len)(实测:1e6 宇宙派发 1109.7s,399418 次执行 2.78ms/
   次,量级与簇长吻合;mods 链表 O(n²) 修复后此退化为剩余主导项)。乘大奇数
   (Fibonacci 散列)+ 异或高半打破位模式。 */
static inline u64 ws_hash_key(u64 fn_id) {
  return (fn_id >> 32) ^ (fn_id * 0x9E3779B97F4A7C15ull);
}

static int ws_grow(mosaic_runtime *rt) {
  u64 cap = rt->ws.cap ? rt->ws.cap * 2 : 16;
  u64 *keys = calloc(cap, sizeof *keys);
  struct mosaic_fn_obj **vals = calloc(cap, sizeof *vals);
  if (!keys || !vals) { free(keys); free(vals); rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
  for (u64 i = 0; i < rt->ws.cap; i++) {
    u64 k = rt->ws.keys[i];
    if (!k) continue;
    u64 h = ws_hash_key(k) & (cap - 1);
    while (keys[h]) h = (h + 1) & (cap - 1);
    keys[h] = k; vals[h] = rt->ws.vals[i];
  }
  free(rt->ws.keys); free(rt->ws.vals);
  rt->ws.keys = keys; rt->ws.vals = vals; rt->ws.cap = cap;
  return 0;
}

struct mosaic_fn_obj *ws_find(mosaic_runtime *rt, u64 fn_id) {
  if (!rt->ws.cap) return NULL;
  u64 h = ws_hash_key(fn_id) & (rt->ws.cap - 1);
  for (u64 i = 0; i < rt->ws.cap; i++) {
    u64 k = rt->ws.keys[h];
    if (!k) return NULL;
    if (k == fn_id) return rt->ws.vals[h];
    h = (h + 1) & (rt->ws.cap - 1);
  }
  return NULL;
}

void ws_insert(mosaic_runtime *rt, struct mosaic_fn_obj *fn) {
  if (ws_find(rt, fn->fn_id)) return;
  if (rt->ws.len * 10 >= rt->ws.cap * 7) {
    if (ws_grow(rt) != 0) return;   /* OOM:ws_grow 已设 last_err,跳过插入 */
  }
  u64 h = ws_hash_key(fn->fn_id) & (rt->ws.cap - 1);
  while (rt->ws.keys[h]) h = (h + 1) & (rt->ws.cap - 1);
  rt->ws.keys[h] = fn->fn_id; rt->ws.vals[h] = fn;
  rt->ws.len++;
  /* 窗口链表(无序;驱逐时全扫描) */
  fn->next = rt->ws_head;
  fn->prev = NULL;
  if (rt->ws_head) rt->ws_head->prev = fn;
  rt->ws_head = fn;
  if (!rt->ws_tail) rt->ws_tail = fn;
}

/* ideal ∈ (h, j](环向前向区间)?h<j 时为 {h+1..j},h>j 时为 {h+1..cap-1, 0..j} */
static int in_fwd_interval(u64 ideal, u64 h, u64 j, u64 mask) {
  if (h < j) return ideal > h && ideal <= j;
  return ideal > h || ideal <= j;
}

void ws_remove(mosaic_runtime *rt, struct mosaic_fn_obj *fn) {
  if (rt->ws.cap) {
    u64 h = ws_hash_key(fn->fn_id) & (rt->ws.cap - 1);
    u64 found = rt->ws.cap;   /* 记录命中槽,cap 表示未命中 */
    for (u64 i = 0; i < rt->ws.cap; i++) {
      if (rt->ws.keys[h] == fn->fn_id) { found = h; break; }
      if (!rt->ws.keys[h]) break;
      h = (h + 1) & (rt->ws.cap - 1);
    }
    if (found != rt->ws.cap) {
      rt->ws.keys[found] = 0; rt->ws.vals[found] = NULL;
      rt->ws.len--;
      /* 关键:开放寻址删除必须后移簇内后续条目,否则簇内靠后的键
         会因遇到空槽而不可达(经典 bug,见自审记录) */
      u64 mask = rt->ws.cap - 1;
      u64 j = (found + 1) & mask;
      while (rt->ws.keys[j]) {
        u64 ideal = ws_hash_key(rt->ws.keys[j]) & mask;
        if (!in_fwd_interval(ideal, found, j, mask)) {
          rt->ws.keys[found] = rt->ws.keys[j];
          rt->ws.vals[found] = rt->ws.vals[j];
          rt->ws.keys[j] = 0; rt->ws.vals[j] = NULL;
          found = j;
        }
        j = (j + 1) & mask;
      }
    }
  }
  if (fn->prev) fn->prev->next = fn->next; else if (rt->ws_head == fn) rt->ws_head = fn->next;
  if (fn->next) fn->next->prev = fn->prev; else if (rt->ws_tail == fn) rt->ws_tail = fn->prev;
  fn->prev = fn->next = NULL;
}
