/* src/deps.c — M2-1:依赖图遍历与闭包解析(规格第 7 节事务 validate 的前置能力)
 *
 * 依赖表(mosaic_dep_entry,16B:(owner_id, dep_id))按 owner 排序,存储于
 * 所属 pack 内;模块记录的 dep_off 指向本 pack 依赖表首条依赖下标(无依赖 =
 * MOSAIC_DEP_NONE)。dep_id 是全局 module_id,可指向其他 pack 的模块——
 * 跨 pack 依赖合法,闭包解析在 open_many 的合并视图上通过 find_module_ex
 * (范围表二分 → pack 表二分)定位依赖模块。
 *
 * M1 格式限制(规格第 7 节依赖表只有 (owner, dep) 两个字段,无约束字段):
 * 解析器对依赖版本不做约束(默认为无界,接受任何版本),只检查存在性与环;
 * self_constraint 只约束入口模块自身。完整版本约束支持依赖 v3 格式为依赖
 * 条目预留约束字段——插入点已在本文件标注(依赖模块入栈处),届时按条目
 * 约束检查版本即可。 */
#include "mosaic_internal.h"
#include "mosaic/deps.h"
#include <stdlib.h>

/* ---- 依赖区间:module 在本 pack 依赖表内的 [start, end) ----
   从 dep_off 起逐条读 md_owner_id == module_id 的连续区间(依赖表按 owner
   排序),直到 owner 变化或表尾;无依赖(dep_off = NONE 或越界)→ 空区间。 */
static void module_dep_range(mosaic_runtime *rt, size_t pack, const mosaic_module_record *m,
                             u64 *out_start, u64 *out_end) {
  const u8 *map = rt->packs[pack].map;
  u64 dc = hdr_dep_count(map);
  u32 off = mm_dep_off(m);
  if (off == MOSAIC_DEP_NONE || (u64)off >= dc) { *out_start = 0; *out_end = 0; return; }
  const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(map + hdr_dep_off(map));
  u64 id = mm_id(m), i = off;
  while (i < dc && md_owner_id(&deps[i]) == id) i++;
  *out_start = off;
  *out_end = i;
}

int mosaic_module_for_each_dep(mosaic_runtime *rt, u64 module_id, mosaic_dep_cb cb, void *user) {
  if (!rt || !cb) return -1;
  size_t pack = 0;
  const mosaic_module_record *m = find_module_ex(rt, module_id, &pack);
  if (!m) return -1;               /* last_err = NOT_FOUND(由 find_module_ex 置) */
  u64 s, e;
  module_dep_range(rt, pack, m, &s, &e);
  if (s == e) return 0;            /* 无依赖 → 直接返回 0,回调零次 */
  const u8 *map = rt->packs[pack].map;
  const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(map + hdr_dep_off(map));
  for (u64 i = s; i < e; i++) {
    int rc = cb(md_dep_id(&deps[i]), user);
    if (rc) return rc;             /* 回调要求停止:透传其非 0 值 */
  }
  return 0;
}

/* 版本是否满足约束:0 = 该端无界 */
static int version_ok(const mosaic_version_constraint *c, u32 version) {
  if (c->min_version && version < c->min_version) return 0;
  if (c->max_version && version > c->max_version) return 0;
  return 1;
}

/* ---- 三色标记(白=未访问,灰=DFS 栈上,黑=已出栈)的开放寻址哈希 ----
   键 = module_id + 1(module_id = 0 合法,0 作空槽哨兵);容量恒为 2 的幂,
   & (cap-1) 取模,70% 负载扩容 ×2 重散列(与 ws_hash/mods_hash 同款)。 */
#define COLOR_WHITE 0
#define COLOR_GRAY  1
#define COLOR_BLACK 2

struct dep_color_map {
  u64 cap, len;
  u64 *keys;               /* module_id + 1;0 = 空槽 */
  u8  *vals;               /* COLOR_* */
};

static void color_map_free(struct dep_color_map *cm) {
  free(cm->keys);
  free(cm->vals);
  cm->keys = NULL; cm->vals = NULL; cm->cap = cm->len = 0;
}

static int color_map_grow(struct dep_color_map *cm) {
  u64 ncap = cm->cap ? cm->cap * 2 : 16;
  u64 *nk = calloc((size_t)ncap, sizeof *nk);
  u8 *nv = calloc((size_t)ncap, sizeof *nv);
  if (!nk || !nv) { free(nk); free(nv); return -1; }
  for (u64 i = 0; i < cm->cap; i++) {
    if (!cm->keys[i]) continue;
    u64 k = cm->keys[i], j = k & (ncap - 1);
    while (nk[j]) j = (j + 1) & (ncap - 1);
    nk[j] = k; nv[j] = cm->vals[i];
  }
  free(cm->keys); free(cm->vals);
  cm->keys = nk; cm->vals = nv; cm->cap = ncap;
  return 0;
}

static int color_map_put(struct dep_color_map *cm, u64 module_id, u8 color) {
  if (cm->len * 10 >= cm->cap * 7 && color_map_grow(cm)) return -1;
  u64 k = module_id + 1, j = k & (cm->cap - 1);
  while (cm->keys[j]) {
    if (cm->keys[j] == k) { cm->vals[j] = color; return 0; }
    j = (j + 1) & (cm->cap - 1);
  }
  cm->keys[j] = k; cm->vals[j] = color; cm->len++;
  return 0;
}

static int color_map_get(const struct dep_color_map *cm, u64 module_id, u8 *out) {
  u64 k = module_id + 1, j = k & (cm->cap - 1);
  while (cm->keys[j]) {
    if (cm->keys[j] == k) { *out = cm->vals[j]; return 1; }
    j = (j + 1) & (cm->cap - 1);
  }
  return 0;
}

/* DFS 帧:模块在其**本 pack** 依赖表内的迭代游标 */
struct dep_frame {
  u64 id;
  size_t pack;
  u64 dep_idx, dep_end;
};

int mosaic_dep_resolve(mosaic_runtime *rt, u64 module_id, const mosaic_version_constraint *self_constraint,
                       u64 *out, size_t out_cap, size_t *out_len) {
  if (out_len) *out_len = 0;
  if (!rt) return -1;
  size_t root_pack = 0;
  const mosaic_module_record *root = find_module_ex(rt, module_id, &root_pack);
  if (!root) return -1;            /* NOT_FOUND(由 find_module_ex 置) */

  /* 入口模块自身受 self_constraint 约束(NULL = 无约束);违反 → ABI */
  if (self_constraint && !version_ok(self_constraint, mm_version(root))) {
    rt->last_err = MOSAIC_ERR_ABI;
    return -1;
  }

  struct dep_color_map cm = {0};
  struct dep_frame *frames = NULL;
  u64 frame_cap = 0, frame_len = 0;
  u64 *clos = NULL, clos_cap = 0, clos_len = 0;
  int rc = -1;

  if (color_map_put(&cm, module_id, COLOR_GRAY)) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
  {
    u64 s, e;
    module_dep_range(rt, root_pack, root, &s, &e);
    frames = malloc(sizeof *frames);
    if (!frames) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
    frame_cap = frame_len = 1;
    frames[0] = (struct dep_frame){ module_id, root_pack, s, e };
  }

  while (frame_len) {
    struct dep_frame *f = &frames[frame_len - 1];
    if (f->dep_idx < f->dep_end) {
      const u8 *map = rt->packs[f->pack].map;
      const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(map + hdr_dep_off(map));
      u64 dep = md_dep_id(&deps[f->dep_idx++]);
      u8 color;
      if (!color_map_get(&cm, dep, &color)) {
        /* 白 → 首次到达:依赖模块必须存在(find_module_ex 全局命中) */
        size_t dp = 0;
        const mosaic_module_record *drec = find_module_ex(rt, dep, &dp);
        if (!drec) goto done;      /* NOT_FOUND(由 find_module_ex 置) */
        /* 版本约束插入点:M1 依赖条目无约束字段 → 一律接受(无界);
           v3 格式为条目增加约束字段后,此处改为按条目约束检查版本
           (version_ok(&unbounded, mm_version(drec)) 恒真,略)。 */
        if (color_map_put(&cm, dep, COLOR_GRAY)) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
        u64 s, e;
        module_dep_range(rt, dp, drec, &s, &e);
        if (frame_len == frame_cap) {
          u64 nc = frame_cap ? frame_cap * 2 : 4;
          struct dep_frame *nf = realloc(frames, (size_t)nc * sizeof *nf);
          if (!nf) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
          frames = nf;
          frame_cap = nc;
        }
        frames[frame_len++] = (struct dep_frame){ dep, dp, s, e };
      } else if (color == COLOR_GRAY) {
        rt->last_err = MOSAIC_ERR_ILLEGAL;   /* 灰 → 回边 → 环 */
        goto done;
      }
      /* 黑:已出栈入闭包,跳过 */
    } else {
      /* 出栈:置黑 + 追加到闭包(后序 → 依赖先于依赖者) */
      u64 id = frames[frame_len - 1].id;
      if (color_map_put(&cm, id, COLOR_BLACK)) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
      if (clos_len == clos_cap) {
        u64 nc = clos_cap ? clos_cap * 2 : 16;
        u64 *n = realloc(clos, (size_t)nc * sizeof *n);
        if (!n) { rt->last_err = MOSAIC_ERR_NOMEM; goto done; }
        clos = n;
        clos_cap = nc;
      }
      clos[clos_len++] = id;
      frame_len--;
    }
  }

  if (out_len) *out_len = clos_len;
  if (out_cap == 0 || !out || clos_len <= out_cap) {   /* 探测或容量足够 */
    if (out && clos_len) memcpy(out, clos, (size_t)clos_len * sizeof *out);
    rc = 0;
  } else {
    rt->last_err = MOSAIC_ERR_NOMEM;   /* 容量不足:out_len 已给出所需长度 */
    rc = -1;
  }
done:
  free(frames);
  free(clos);
  color_map_free(&cm);
  return rc;
}
