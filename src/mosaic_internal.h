#ifndef MOSAIC_INTERNAL_H
#define MOSAIC_INTERNAL_H
#include "mosaic/runtime.h"
#include "mosaic/module.h"
#include "mosaic/function.h"
#include "mosaic/event.h"
#include "mosaic/ownership.h"
#include "mosaic/eviction.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

struct mod_entry {
  u64 module_id;
  void *so;
  const mosaic_module_abi *abi;
  u32 refs;
  u8 pending;   /* 缺陷 2:refs 归零 → 待 flush 安全点 dlclose(自墓碑正在执行的
                   .so 不得立即卸载,否则返回地址悬垂 → 返回即崩);flush 前
                   .so 保持加载,mod_load 命中 pending 条目直接复活(不重新 dlopen) */
};

/* 已 dlopen 模块的开放寻址哈希(线性探测):mod_load/mod_unload 由单链表全链
   扫描 O(n) 降为 O(1)(评审已知缺陷:mods 链表,全量物化 O(n_mods²),1e6
   函数实测 1109.7s)。与 ws_hash 同款:0 = 空槽(module_id ≥ 1,哨兵安全),
   容量恒为 2 的幂,& (cap-1) 取模,负载 70% 时扩容 ×2 重散列。 */
struct mods_hash {
  u64 cap, len;
  u64 *keys;               /* 0 = 空槽 */
  struct mod_entry **vals;
};

struct ws_hash {
  u64 cap, len;
  u64 *keys;               /* 0 = 空槽 */
  struct mosaic_fn_obj **vals;
};

struct slab {
  struct slab *next;
  u8 *start, *end, *cur;
  struct mosaic_fn_obj *free_head;
};

/* M1.5-A:单 fd/map 改为 pack 数组。每个 pack 是独立 mmap(fd/map/map_len);
   state_len(状态 blob 追加游标)随 pack 走——state_blob_append 只 mremap 本 pack
   的映射,其他 pack 指针不受影响。min_mod/max_mod 为本 pack 模块 id 范围
   (模块表按 id 排序,取首/末条);空模块表 → 范围 (1, 0)(min > max,空区间,
   排序靠前,任何重叠判定都不命中)。open_many 校验后按 (min, max) 排序,
   rt->packs 顺序即范围表顺序。 */
struct pack_view {
  int fd;
  u8 *map;
  size_t map_len;
  u64 state_len;
  u64 min_mod, max_mod;
};

/* genroute.c — fn_id → 活跃 generation 路由(开放寻址,复用 ws_hash 键位混合
   纪律):默认(无条目)= 该 fn 在基础 pack 中的原始记录(generation 最低);
   put 建立/覆盖路由;swap 原子替换整表(commit 用,旧表指针保留供 rollback
   demote)。 */
struct gen_route {
  u64 cap, len;
  u64 *keys;      /* fn_id,0 = 空槽 */
  u32 *gens;      /* 活跃 generation */
};
int gen_route_put(struct gen_route *t, u64 fn_id, u32 gen);   /* 扩容失败 → -1 */
u32 gen_route_get(const struct gen_route *t, u64 fn_id);      /* 0 = 无条目 */
struct gen_route *gen_route_swap(struct gen_route **slot, struct gen_route *new_table);
/* swap 返回旧表指针(调用方持有以支持 rollback);新表由调用方构造并填充 */
void gen_route_free(struct gen_route *t);

struct mosaic_runtime {
  struct pack_view *packs;
  size_t n_packs;
  struct mods_hash mods;   /* 已 dlopen 的模块(开放寻址哈希) */
  u32 dispatch_depth;      /* 嵌套派发深度:flush_pending_dlclose 只在最外层
                              派发末尾执行(此时栈上无任何模块代码帧,dlclose
                              才安全);内层派发末尾只减深度不 flush */
  struct ws_hash ws;
  struct mosaic_fn_obj *ws_head, *ws_tail;
  struct slab *slabs;
  struct gen_route *routes;   /* fn_id → 活跃 generation 路由表(NULL = 无更新) */
  u32 last_err;
};

/* 按 pack 访问映射;越界返回 NULL */
static inline u8 *pack_map(mosaic_runtime *rt, size_t i) {
  if (!rt || i >= rt->n_packs) return NULL;
  return rt->packs[i].map;
}

static inline u64 now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

/* working_set.c */
struct mosaic_fn_obj *ws_find(mosaic_runtime *rt, u64 fn_id);
void ws_insert(mosaic_runtime *rt, struct mosaic_fn_obj *fn);
void ws_remove(mosaic_runtime *rt, struct mosaic_fn_obj *fn);
struct mosaic_fn_obj *fn_alloc(mosaic_runtime *rt);
void fn_free(mosaic_runtime *rt, struct mosaic_fn_obj *fn);
void *arena_alloc(mosaic_runtime *rt, size_t n);
void arena_zalloc(mosaic_runtime *rt, size_t n, void **out);

/* index.c — M1.5-A:ex 变体带 pack 输出(范围表二分 → 该 pack 表二分) */
const mosaic_module_record *find_module_ex(mosaic_runtime *rt, u64 module_id, size_t *out_pack);
const mosaic_function_record *find_function_ex(mosaic_runtime *rt, u64 fn_id, size_t *out_pack);

/* runtime.c — M1.5-A:ex 变体带 pack 参数(不依赖指针扫描) */
const char *module_string_ex(const mosaic_runtime *rt, size_t pack, const mosaic_module_record *m, u32 off);

/* lifecycle.c — pack 参数:只对该 pack 的 map mremap */
const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id);
void mod_unload(mosaic_runtime *rt, u64 module_id);
int state_blob_append(mosaic_runtime *rt, size_t pack, const void *bytes, u32 len, u32 *out_off);
/* 延迟 dlclose(缺陷 2):dlclose + free 所有 pending 条目并重建哈希表。
   只在安全点调用(dispatch/evict 末尾、close 前)——此时栈上无模块代码帧。 */
void flush_pending_dlclose(mosaic_runtime *rt);
#endif
