/* src/genroute.c — M2-2a:fn_id → 活跃 generation 路由表。
 *
 * 语义:默认(无条目)= 该 fn 在基础 pack 中的原始记录(generation 最低);
 * put 建立/覆盖路由(事务 commit 逐个函数切到新 generation 时填充);
 * swap 原子替换整表指针(commit 切换点;旧表指针返回给调用方持有,
 * rollback = demote 时把旧表换回)。M2-2b(tx API)消费。 */
#include "mosaic_internal.h"
#include <stdlib.h>

/* 键位混合:与 ws_hash 同纪律(fn_id = (module<<32)|local,低 32 位几乎恒定;
   乘大奇数 Fibonacci 散列 + 异或高半打破位模式,避免 fn_id 模式塌缩成
   少量槽 → 线性探测簇退化 O(n))。 */
static inline u64 gen_route_key(u64 fn_id) {
  return (fn_id >> 32) ^ (fn_id * 0x9E3779B97F4A7C15ull);
}

/* 开放寻址哈希(线性探测):0 = 空槽(fn_id ≥ 1 且 gen ≥ 1,哨兵安全,
   与 ws_hash/mods_hash 同款);容量恒为 2 的幂,& (cap-1) 取模;
   负载 ≥70% 扩容 ×2 重散列。失败时旧表不变(分配先行)。 */
static int gen_route_grow(struct gen_route *t) {
  u64 cap = t->cap ? t->cap * 2 : 16;
  u64 *keys = calloc(cap, sizeof *keys);
  u32 *gens = calloc(cap, sizeof *gens);
  if (!keys || !gens) { free(keys); free(gens); return -1; }
  for (u64 i = 0; i < t->cap; i++) {
    u64 k = t->keys[i];
    if (!k) continue;
    u64 j = gen_route_key(k) & (cap - 1);
    while (keys[j]) j = (j + 1) & (cap - 1);
    keys[j] = k; gens[j] = t->gens[i];
  }
  free(t->keys); free(t->gens);
  t->keys = keys; t->gens = gens; t->cap = cap;
  return 0;
}

/* 建立/覆盖路由。gen == 0 拒绝(0 是 get 的"无条目"哨兵,写入会造成
   条目存在但查不到的空洞语义);fn_id == 0 拒绝(与空槽哨兵对称,M2-2a 评审
   Minor-1:fn_id=0 写入会创建"条目存在但探测遇空槽即停"的不可达空洞,
   并虚增 len 破坏负载扩容判定)。扩容失败 → -1(旧表完好)。 */
int gen_route_put(struct gen_route *t, u64 fn_id, u32 gen) {
  if (!t || gen == 0 || fn_id == 0) return -1;
  if (t->cap) {
    u64 mask = t->cap - 1;
    u64 i = gen_route_key(fn_id) & mask;
    for (u64 n = 0; n < t->cap; n++) {
      u64 k = t->keys[i];
      if (!k) break;
      if (k == fn_id) { t->gens[i] = gen; return 0; }   /* 覆盖已有路由 */
      i = (i + 1) & mask;
    }
  }
  if (t->len * 10 >= t->cap * 7) {        /* cap==0 时 0>=0 恒真 → 首插先建表 */
    if (gen_route_grow(t) != 0) return -1;
  }
  u64 mask = t->cap - 1;
  u64 i = gen_route_key(fn_id) & mask;
  while (t->keys[i]) i = (i + 1) & mask;
  t->keys[i] = fn_id; t->gens[i] = gen;
  t->len++;
  return 0;
}

/* 查询:0 = 无条目(调用方回落到基础 pack 原始记录,generation 最低) */
u32 gen_route_get(const struct gen_route *t, u64 fn_id) {
  if (!t || !t->cap) return 0;
  u64 mask = t->cap - 1;
  u64 i = gen_route_key(fn_id) & mask;
  for (u64 n = 0; n < t->cap; n++) {
    u64 k = t->keys[i];
    if (!k) return 0;
    if (k == fn_id) return t->gens[i];
    i = (i + 1) & mask;
  }
  return 0;
}

/* 原子替换整表:slot 指向新表,返回旧表指针(调用方持有以支持 rollback
   demote)。新表由调用方构造并填充;NULL slot 同样成立(首次 commit)。 */
struct gen_route *gen_route_swap(struct gen_route **slot, struct gen_route *new_table) {
  struct gen_route *old = *slot;
  *slot = new_table;
  return old;
}

/* 释放表内部数组并清零(表体由持有者分配,如是堆表则另行 free) */
void gen_route_free(struct gen_route *t) {
  if (!t) return;
  free(t->keys); free(t->gens);
  t->keys = NULL; t->gens = NULL;
  t->cap = t->len = 0;
}
