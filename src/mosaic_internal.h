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
  struct mod_entry *next;   /* M2-2b 修复(I-1):失效条目链——commit 使 mods
                               条目失效时,refs>0 的旧 .so 不能立即 dlclose,
                               从哈希摘除后挂 rt->mods_dead,refs 由 mod_unload
                               递减、归零置 pending,flush_pending_dlclose 收尾 */
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
  struct mod_entry *mods_dead;   /* M2-2b 修复(I-1):commit 失效的旧 .so 条目链
                                    (refs>0 不能立即 dlclose,延迟释放) */
  u32 dispatch_depth;      /* 嵌套派发深度:flush_pending_dlclose 只在最外层
                              派发末尾执行(此时栈上无任何模块代码帧,dlclose
                              才安全);内层派发末尾只减深度不 flush */
  struct ws_hash ws;
  struct mosaic_fn_obj *ws_head, *ws_tail;
  struct slab *slabs;
  struct gen_route *routes;   /* fn_id → 活跃 generation 路由表(NULL = 无更新) */
  /* M2-2b:已 commit 补丁 pack(结构同 pack_view;不进范围表——find_module_ex
     等基础查询不感知;find_function_active/find_module_active 在其上解析活跃
     代记录)。顺序 = commit 顺序;rollback 从中移除 + unmap。 */
  struct pack_view *tx_packs;
  size_t n_tx_packs;
  u32 last_err;
};

/* pack 下标解析:0..n_packs-1 = 基础 pack,>= n_packs = tx_packs[i - n_packs]
   (find_function_active/find_module_active 的 out_pack 编码;lifecycle 的 blob
   读写与 state_blob_append 经此统一解析,补丁记录的状态落补丁 pack blob)。 */
static inline struct pack_view *pack_view(mosaic_runtime *rt, size_t i) {
  if (!rt) return NULL;
  if (i < rt->n_packs) return &rt->packs[i];
  if (i - rt->n_packs < rt->n_tx_packs) return &rt->tx_packs[i - rt->n_packs];
  return NULL;
}

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

/* index.c — M2-2b:活跃代记录解析。find_function_active:gen_route 命中 → 在
   rt->tx_packs 中二分定位该 generation 的记录(补丁每 fn 单条,核对
   mf_generation;out_pack = n_packs + tx 下标);未命中 → 既有 find_function_ex。
   find_module_active:先查 tx_packs(已 commit 补丁的模块记录,so_path 等以补丁
   为准),未命中回落基础 pack。 */
const mosaic_function_record *find_function_active(mosaic_runtime *rt, u64 fn_id, size_t *out_pack);
const mosaic_module_record *find_module_active(mosaic_runtime *rt, u64 module_id, size_t *out_pack);

/* trigger.c — 触发表下界(第一个 event_id >= ev 的条目下标;逐 pack 区间扫描
   用)。M6-B:bridge.c(triggerSubscribers 列出事件订阅者)复用,免复制二分。 */
u64 trigger_lower_bound(const u8 *map, u32 ev);

/* runtime.c — 布局/事件表校验(M2-2b tx_begin 复用:补丁 pack 是独立 mmap,
   校验思路与 open_many 逐 pack 一致) */
int validate_layout(mosaic_runtime *rt, const u8 *map, size_t map_len,
                    char *errbuf, size_t errlen);
int event_tables_match(const struct pack_view *a, const struct pack_view *b);

/* runtime.c — M1.5-A:ex 变体带 pack 参数(不依赖指针扫描) */
const char *module_string_ex(const mosaic_runtime *rt, size_t pack, const mosaic_module_record *m, u32 off);

/* lifecycle.c — pack 参数:只对该 pack 的 map mremap;pack 经 pack_view 解析
   (基础 pack 与 tx_packs 统一)。state_blob_append_pack:任意 pack_view 的核心
   实现(M2-2b commit 写补丁 pack blob 用,补丁未转持前不在 rt->tx_packs) */
const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id);
void mod_unload(mosaic_runtime *rt, u64 module_id);
/* M2-2b 修复(I-1):commit 转持后调用——补丁模块的 mods 缓存条目失效(旧 .so
   + 旧 abi),下次 mod_load 重新 dlopen 补丁 so_path。哈希无逐条删除,重建式
   (标记槽位 + mods_compact,commit 罕见,O(n) 可接受)。refs>0 的旧 .so 挂
   mods_dead 链延迟释放(与延迟 dlclose 同一纪律)。 */
void mods_invalidate(mosaic_runtime *rt, u64 module_id);
int state_blob_append(mosaic_runtime *rt, size_t pack, const void *bytes, u32 len, u32 *out_off);
int state_blob_append_pack(mosaic_runtime *rt, struct pack_view *pv, const void *bytes, u32 len, u32 *out_off);
/* 延迟 dlclose(缺陷 2):dlclose + free 所有 pending 条目并重建哈希表(含
   mods_dead 链上的失效条目)。只在安全点调用(dispatch/evict 末尾、close
   前)——此时栈上无模块代码帧。 */
void flush_pending_dlclose(mosaic_runtime *rt);
#endif
