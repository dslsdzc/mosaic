/* src/tx.c — M2-2b:补丁 pack 事务 API(设计规格第 7 节"事务 / 滚动更新"完整落地)。
 *
 * 流程:begin(mmapping + begin 级校验)→ validate(依赖闭包 + ABI 只读探测 +
 * transform/code_off 越界)→ commit(原子切换:状态迁移 + quiesce + 路由切换 +
 * 补丁转持)→ rollback(demote)/ abort(未提交无副作用)。free 是唯一释放入口。
 *
 * 关键设计点:
 * - 补丁 pack 是独立 mmap(不经过 open_many、不进范围表),校验复用 runtime.c
 *   的 validate_layout / event_tables_match。
 * - 状态迁移:v1 state 取活对象(base 记录已由墓碑序列化,活对象在 ws 里还
 *   活着)或 base blob;transform 钩子写 v2 缓冲,结果落**补丁 pack** blob
 *   (state_blob_append_pack,I-1 纪律:append 后 mremap 可能移动补丁映射,
 *   记录指针必须重取)。
 * - 顺序:迁移(读活对象,对象还活着)→ quiesce(墓碑写 v1 blob,保 v1 状态
 *   供 demote)→ 路由切换 → 补丁转持。注意 quiesce 必须在路由切换**之前**:
 *   lifecycle 的墓碑按 find_function_active 解析记录,切换后解析到补丁记录
 *   (COLD → ILLEGAL 且会把 v1 状态写进补丁 blob)——这是对简报"先 swap 后
 *   quiesce"的顺序修正,语义等价且不变量更强(失败路径不需要撤销墓碑)。
 * - 新路由表 = 当前路由副本 + 补丁条目:多次 commit 时既有补丁的路由不丢失
 *   (简报未显式要求,但"新表只含本补丁"会让先前 commit 的补丁静默 demote)。
 * - 探测结果缓存于 tx(so + abi),commit 用其 transform 后 dlclose(物化会
 *   重新 dlopen);validate 失败或 abort 时同样 dlclose。 */
#include "mosaic_internal.h"
#include "mosaic/tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

struct tx_probe {
  u64 module_id;
  void *so;
  const mosaic_module_abi *abi;
};

struct mosaic_tx {
  mosaic_runtime *rt;
  struct pack_view patch;      /* 补丁 pack 独立 mmap(fd/map/map_len/state_len) */
  struct tx_probe *probes;     /* validate 的 ABI 只读探测缓存(so + abi) */
  size_t n_probes;
  struct gen_route *old_routes;   /* commit 保存的旧表(rollback demote 用;
                                     commit 失败路径的新表就地释放) */
  int committed;                  /* commit 成功(补丁已转持) */
  int transferred;                /* 补丁 pack 已在 rt->tx_packs */
  int rolled_back;                /* rollback(demote)已完成 */
  int released;                   /* 补丁 mmap/探测已释放(防重复 munmap/dlclose) */
};

static void tx_err(mosaic_runtime *rt, u32 code, char *errbuf, size_t errlen, const char *msg) {
  if (rt) rt->last_err = code;
  if (errbuf && errlen) snprintf(errbuf, errlen, "%s", msg);
}

/* ---- 补丁 pack 记录访问 ---- */

static const char *tx_pack_string(const struct pack_view *pv, u32 off) {
  if (!pv || off == 0) return NULL;
  u64 base = hdr_meta_off(pv->map);
  if (base + off >= pv->map_len) return NULL;
  return (const char *)(pv->map + base + off);
}

/* 补丁 fn 表按 fn_id 二分(补丁每 fn 单条,begin 已校验;MAP_PRIVATE 可写) */
static mosaic_function_record *tx_find_patch_fn(mosaic_tx *tx, u64 fn_id) {
  const u8 *map = tx->patch.map;
  u64 n = hdr_fn_count(map);
  mosaic_function_record *fns = (mosaic_function_record *)(map + hdr_fn_off(map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mf_id(&fns[mid]);
    if (id == fn_id) return &fns[mid];
    if (id < fn_id) lo = mid + 1; else hi = mid;
  }
  return NULL;
}

static struct tx_probe *tx_find_probe(mosaic_tx *tx, u64 module_id) {
  for (size_t i = 0; i < tx->n_probes; i++)
    if (tx->probes[i].module_id == module_id) return &tx->probes[i];
  return NULL;
}

static void tx_release_probes(mosaic_tx *tx) {
  for (size_t i = 0; i < tx->n_probes; i++)
    if (tx->probes[i].so) dlclose(tx->probes[i].so);
  free(tx->probes);
  tx->probes = NULL;
  tx->n_probes = 0;
}

/* ---- begin:补丁 pack mmap + begin 级校验 ---- */

mosaic_tx *mosaic_tx_begin(mosaic_runtime *rt, const char *tx_pack_path, char *errbuf, size_t errlen) {
  if (!rt || !tx_pack_path) {
    if (errbuf && errlen) snprintf(errbuf, errlen, "bad args");
    return NULL;
  }
  mosaic_tx *tx = calloc(1, sizeof *tx);
  if (!tx) { if (errbuf && errlen) snprintf(errbuf, errlen, "oom"); return NULL; }
  tx->rt = rt;
  tx->patch.fd = -1;
  /* 独立 mmap(不经过 open_many):O_RDWR 先试(commit 写 blob 需要;回退
     O_RDONLY 时 commit 写路径经 ftruncate 失败优雅降级),校验思路与
     runtime.c 逐 pack 一致 */
  int fd = open(tx_pack_path, O_RDWR);
  if (fd < 0) fd = open(tx_pack_path, O_RDONLY);
  if (fd < 0) { if (errbuf && errlen) snprintf(errbuf, errlen, "open %s failed", tx_pack_path); goto fail; }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < HDR_SIZE) {
    close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "pack too small"); goto fail;
  }
  size_t len = (size_t)st.st_size;
  void *map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) { close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "mmap failed"); goto fail; }
  tx->patch.fd = fd;
  tx->patch.map = map;
  tx->patch.map_len = len;
  tx->patch.state_len = hdr_state_len(map);

  if (validate_layout(rt, map, len, errbuf, errlen) != 0) goto fail;   /* 魔数/版本/偏移 */
  if (rt->n_packs == 0) { tx_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "no base packs"); goto fail; }
  /* 事件表必须与 base pack 0 完全一致(事件是宇宙级全局命名空间) */
  if (!event_tables_match(&rt->packs[0], &tx->patch)) {
    tx_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "tx event table mismatch");
    goto fail;
  }
  u64 mc = hdr_module_count(map);
  const mosaic_module_record *pmods = (const mosaic_module_record *)(map + hdr_module_off(map));
  /* 补丁内每个模块 id 必须已存在于 base */
  for (u64 i = 0; i < mc; i++) {
    if (!find_module_ex(rt, mm_id(&pmods[i]), NULL)) {
      tx_err(rt, MOSAIC_ERR_NOT_FOUND, errbuf, errlen, "tx module not in base");
      goto fail;
    }
  }
  /* 补丁内每个 fn:必须存在于 base;同 fn_id 只能一条;generation 必须比
     base 当前活跃代(gen_route 命中或 base 记录 mf_generation)新 */
  u64 nf = hdr_fn_count(map);
  const mosaic_function_record *pfns = (const mosaic_function_record *)(map + hdr_fn_off(map));
  u64 prev_id = 0;
  for (u64 i = 0; i < nf; i++) {
    u64 fn_id = mf_id(&pfns[i]);
    const mosaic_function_record *brec = find_function_ex(rt, fn_id, NULL);
    if (!brec) { tx_err(rt, MOSAIC_ERR_NOT_FOUND, errbuf, errlen, "tx fn not in base"); goto fail; }
    if (i && fn_id == prev_id) { tx_err(rt, MOSAIC_ERR_ILLEGAL, errbuf, errlen, "tx duplicate fn"); goto fail; }
    prev_id = fn_id;
    u32 active = rt->routes ? gen_route_get(rt->routes, fn_id) : 0;
    if (!active) active = mf_generation(brec);
    if (mf_generation(&pfns[i]) <= active) {
      tx_err(rt, MOSAIC_ERR_ILLEGAL, errbuf, errlen, "tx generation not newer");
      goto fail;
    }
  }
  /* 模块 version 必须 ≥ base 版本(版本回退拒绝) */
  for (u64 i = 0; i < mc; i++) {
    size_t bp = 0;
    const mosaic_module_record *bmod = find_module_ex(rt, mm_id(&pmods[i]), &bp);
    if (!bmod) { tx_err(rt, MOSAIC_ERR_NOT_FOUND, errbuf, errlen, "tx module not in base"); goto fail; }
    if (mm_version(&pmods[i]) < mm_version(bmod)) {
      tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx version regress");
      goto fail;
    }
  }
  return tx;
fail:
  if (tx->patch.map) munmap(tx->patch.map, tx->patch.map_len);
  if (tx->patch.fd >= 0) close(tx->patch.fd);
  free(tx);
  return NULL;
}

/* ---- 依赖闭包(M2-1 语义复用:存在性 + 环;M1 依赖条目无版本约束字段,
  一律无界;self_constraint 约束入口模块自身——补丁根就是补丁模块自身,
  其声明版本恒满足 {min: 自身版本}) ---- */

struct tx_dep_color { u64 cap, len; u64 *keys; u8 *vals; };   /* 键 = module_id + 1 */
#define TXC_WHITE 0
#define TXC_GRAY  1
#define TXC_BLACK 2

static int tx_color_put(struct tx_dep_color *c, u64 id, u8 v) {
  if (c->len * 10 >= c->cap * 7) {
    u64 ncap = c->cap ? c->cap * 2 : 16;
    u64 *nk = calloc((size_t)ncap, sizeof *nk);
    u8 *nv = calloc((size_t)ncap, sizeof *nv);
    if (!nk || !nv) { free(nk); free(nv); return -1; }
    for (u64 i = 0; i < c->cap; i++) {
      if (!c->keys[i]) continue;
      u64 k = c->keys[i], j = k & (ncap - 1);
      while (nk[j]) j = (j + 1) & (ncap - 1);
      nk[j] = k; nv[j] = c->vals[i];
    }
    free(c->keys); free(c->vals);
    c->keys = nk; c->vals = nv; c->cap = ncap;
  }
  u64 k = id + 1, j = k & (c->cap - 1);
  while (c->keys[j]) {
    if (c->keys[j] == k) { c->vals[j] = v; return 0; }
    j = (j + 1) & (c->cap - 1);
  }
  c->keys[j] = k; c->vals[j] = v; c->len++;
  return 0;
}

static int tx_color_get(const struct tx_dep_color *c, u64 id, u8 *out) {
  u64 k = id + 1, j = k & (c->cap - 1);
  while (c->keys[j]) {
    if (c->keys[j] == k) { *out = c->vals[j]; return 1; }
    j = (j + 1) & (c->cap - 1);
  }
  return 0;
}

struct tx_dep_frame { u64 id; const u8 *map; u64 dep_idx, dep_end; };

/* 定位模块依赖区间 [out_start, out_end) 及其所在 pack 的 map;模块不存在
   (补丁与基础都无)→ NULL。补丁 pack 命中优先——补丁模块的依赖表读补丁
   pack 自身,基础模块读基础 pack(M2-1 的"按模块所在 pack 读"语义) */
static const u8 *tx_locate_deps(mosaic_tx *tx, u64 module_id, const u8 **out_map,
                                u64 *out_start, u64 *out_end) {
  const u8 *pm = tx->patch.map;
  u64 mn = hdr_module_count(pm);
  const mosaic_module_record *mods = (const mosaic_module_record *)(pm + hdr_module_off(pm));
  u64 lo = 0, hi = mn;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mm_id(&mods[mid]);
    if (id == module_id) {
      u64 dc = hdr_dep_count(pm);
      u32 off = mm_dep_off(&mods[mid]);
      u64 s = 0, e = 0;
      if (off != MOSAIC_DEP_NONE && (u64)off < dc) {
        const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(pm + hdr_dep_off(pm));
        s = e = off;
        while (e < dc && md_owner_id(&deps[e]) == module_id) e++;
      }
      *out_map = pm; *out_start = s; *out_end = e;
      return pm;
    }
    if (id < module_id) lo = mid + 1; else hi = mid;
  }
  size_t bp = 0;
  const mosaic_module_record *m = find_module_ex(tx->rt, module_id, &bp);
  if (!m) return NULL;
  const u8 *map = tx->rt->packs[bp].map;
  u64 dc = hdr_dep_count(map);
  u32 off = mm_dep_off(m);
  u64 s = 0, e = 0;
  if (off != MOSAIC_DEP_NONE && (u64)off < dc) {
    const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(map + hdr_dep_off(map));
    s = e = off;
    while (e < dc && md_owner_id(&deps[e]) == module_id) e++;
  }
  *out_map = map; *out_start = s; *out_end = e;
  return map;
}

static int tx_dep_closure(mosaic_tx *tx, u64 root, char *errbuf, size_t errlen) {
  mosaic_runtime *rt = tx->rt;
  struct tx_dep_color cm = { 0 };
  struct tx_dep_frame *frames = NULL;
  u64 fcap = 0, flen = 0;
  int rc = -1;
  const u8 *rmap = NULL;
  u64 s = 0, e = 0;
  if (!tx_locate_deps(tx, root, &rmap, &s, &e)) {
    tx_err(rt, MOSAIC_ERR_NOT_FOUND, errbuf, errlen, "tx dep missing");
    goto done;
  }
  if (tx_color_put(&cm, root, TXC_GRAY) != 0) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); goto done; }
  frames = malloc(sizeof *frames);
  if (!frames) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); goto done; }
  fcap = flen = 1;
  frames[0] = (struct tx_dep_frame){ root, rmap, s, e };
  while (flen) {
    struct tx_dep_frame *f = &frames[flen - 1];
    if (f->dep_idx < f->dep_end) {
      const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(f->map + hdr_dep_off(f->map));
      u64 dep = md_dep_id(&deps[f->dep_idx++]);
      u8 c;
      if (!tx_color_get(&cm, dep, &c)) {
        const u8 *dmap = NULL;
        u64 ds = 0, de = 0;
        if (!tx_locate_deps(tx, dep, &dmap, &ds, &de)) {
          tx_err(rt, MOSAIC_ERR_NOT_FOUND, errbuf, errlen, "tx dep missing");
          goto done;
        }
        if (tx_color_put(&cm, dep, TXC_GRAY) != 0) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); goto done; }
        if (flen == fcap) {
          u64 nc = fcap ? fcap * 2 : 4;
          struct tx_dep_frame *nf = realloc(frames, (size_t)nc * sizeof *nf);
          if (!nf) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); goto done; }
          frames = nf;
          fcap = nc;
        }
        frames[flen++] = (struct tx_dep_frame){ dep, dmap, ds, de };
      } else if (c == TXC_GRAY) {
        tx_err(rt, MOSAIC_ERR_ILLEGAL, errbuf, errlen, "tx dep cycle");
        goto done;
      }
    } else {
      if (tx_color_put(&cm, frames[flen - 1].id, TXC_BLACK) != 0) {
        tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom");
        goto done;
      }
      flen--;
    }
  }
  rc = 0;
done:
  free(frames);
  free(cm.keys);
  free(cm.vals);
  return rc;
}

/* ---- validate ---- */

/* ABI 只读探测:dlopen + dlsym + abi_version 校验;不进入 runtime 的 mods 表
   (探测完由 tx 持有,commit 用其 transform 后 dlclose;物化走 mod_load 正常
   路径会重新 dlopen)。已探测过 → 跳过(validate 可重复调用)。 */
static int tx_probe_module(mosaic_tx *tx, const mosaic_module_record *m, char *errbuf, size_t errlen) {
  mosaic_runtime *rt = tx->rt;
  if (tx_find_probe(tx, mm_id(m))) return 0;
  const char *path = tx_pack_string(&tx->patch, mm_so_off(m));
  void *so = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
  if (!so) { tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx abi probe failed"); return -1; }
  dlerror();
  mosaic_module_abi_v1_fn sym = (mosaic_module_abi_v1_fn)dlsym(so, "mosaic_module_abi_v1");
  if (dlerror() || !sym) { dlclose(so); tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx abi probe failed"); return -1; }
  const mosaic_module_abi *abi = sym();
  if (!abi || abi->abi_version != MOSAIC_MODULE_ABI_VERSION) {
    dlclose(so);
    tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx abi probe failed");
    return -1;
  }
  struct tx_probe *p = realloc(tx->probes, (tx->n_probes + 1) * sizeof *p);
  if (!p) { dlclose(so); tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); return -1; }
  tx->probes = p;
  tx->probes[tx->n_probes++] = (struct tx_probe){ mm_id(m), so, abi };
  return 0;
}

int mosaic_tx_validate(mosaic_tx *tx, char *errbuf, size_t errlen) {
  if (!tx) { if (errbuf && errlen) snprintf(errbuf, errlen, "bad tx"); return -1; }
  if (tx->committed) { if (errbuf && errlen) snprintf(errbuf, errlen, "already committed"); return -1; }
  mosaic_runtime *rt = tx->rt;
  const u8 *map = tx->patch.map;
  u64 mc = hdr_module_count(map);
  const mosaic_module_record *pmods = (const mosaic_module_record *)(map + hdr_module_off(map));
  /* a) 依赖闭包:每个补丁模块(依赖表读补丁 pack 自身;闭包内模块必须全部
     存在 + 无环,缺模块/环 → 失败) */
  for (u64 i = 0; i < mc; i++) {
    if (tx_dep_closure(tx, mm_id(&pmods[i]), errbuf, errlen) != 0) goto fail;
  }
  /* b) ABI 只读探测(所有补丁模块先全部探测成功,供 c/d 与 commit 复用) */
  for (u64 i = 0; i < mc; i++) {
    if (tx_probe_module(tx, &pmods[i], errbuf, errlen) != 0) goto fail;
  }
  /* c) transform 索引越界:REQUIRES_STATE 且 reserved != 0 → reserved-1 <
     abi->transform_count;d) code_off 必须 < abi->fn_count */
  u64 nf = hdr_fn_count(map);
  const mosaic_function_record *pfns = (const mosaic_function_record *)(map + hdr_fn_off(map));
  for (u64 i = 0; i < nf; i++) {
    struct tx_probe *p = tx_find_probe(tx, mf_module_id(&pfns[i]));
    if (!p) { tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx abi probe failed"); goto fail; }
    if ((mf_flags(&pfns[i]) & MOSAIC_FN_REQUIRES_STATE) && mf_reserved(&pfns[i])) {
      if (mf_reserved(&pfns[i]) - 1 >= p->abi->transform_count || !p->abi->transforms) {
        tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx transform index out of range");
        goto fail;
      }
    }
    if (mf_code_off(&pfns[i]) >= p->abi->fn_count) {
      tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx code_off out of range");
      goto fail;
    }
  }
  return 0;
fail:
  tx_release_probes(tx);   /* 失败:释放已探测 .so(tx 只剩 mmap,abort/free 收尾) */
  return -1;
}

/* ---- commit ---- */

/* 状态迁移:每个 REQUIRES_STATE 的补丁函数——
   - v1 state:ws_find 命中 → 活对象 state(对象还活着,直接读);未命中 →
     **活跃记录** mf_state_off → 其归属 pack 的 blob。
   - M2-2b 修复(C-1):死路径曾用 find_function_ex(base 记录 → base blob)。
     已 patch 过的函数在第二次 commit 时,活跃状态在**上一补丁 blob**(其
     mf_state_off 由上次墓碑写在活跃记录上),base blob 只是历史快照(实测
     gen1→commit#1(3→30)→执行 32→墓碑→commit#2 物化 = 30,应为 320)。
     与物化恢复同一来源:find_function_active(路由命中 → 补丁记录,pack 编
     码经 pack_view 解析到 tx_packs;未命中 → base 记录),blob 从活跃记录
     所在 pack 读取。
   - v1 state 不存在(从未执行)→ v2 初始 = 零填充(记录 state_off 保持 0,
     与首次物化一致),不调用 transform。
   - 有变换(reserved != 0):transform(v1_state, v2_state, size),size = v2
     记录 state_size_hint(0 → abi->state_size);无变换:原样拷贝(截断到 v2
     容量)。
   - 结果写**补丁 pack** 的 blob(I-1:append 的 mremap 可能移动补丁映射,
     记录指针在 append 后重取)。 */
static int tx_migrate_state(mosaic_tx *tx, char *errbuf, size_t errlen) {
  mosaic_runtime *rt = tx->rt;
  u64 nf = hdr_fn_count(tx->patch.map);
  for (u64 i = 0; i < nf; i++) {
    /* 每次迭代现算记录指针(前一次 append 可能已移动补丁映射) */
    const mosaic_function_record *f =
        (const mosaic_function_record *)(tx->patch.map + hdr_fn_off(tx->patch.map)) + i;
    if (!(mf_flags(f) & MOSAIC_FN_REQUIRES_STATE)) continue;
    u64 fn_id = mf_id(f);
    void *v1buf = NULL;
    const void *v1 = NULL;
    u32 v1sz = 0;
    int have_v1 = 0;
    mosaic_fn_obj *live = ws_find(rt, fn_id);
    if (live && live->state) {
      v1 = live->state;
      v1sz = live->state_size;
      have_v1 = 1;
    } else {
      /* C-1:活跃代解析(与物化恢复同一来源)——第二次 commit 时活跃状态在
         上一补丁 blob,find_function_ex 只看到 base 历史快照 */
      size_t bp = 0;
      const mosaic_function_record *brec = find_function_active(rt, fn_id, &bp);
      u32 soff = brec ? mf_state_off(brec) : 0;
      if (soff) {
        struct pack_view *bpv = pack_view(rt, bp);
        if (bpv) {
          const u8 *h = bpv->map;
          u64 base = hdr_state_off(h);
          if (base + soff + 4 <= bpv->map_len) {
            u32 len = rd_le32(bpv->map + base + soff);
            if (base + soff + 4 + len <= bpv->map_len) {
              v1buf = malloc(len ? len : 1);
              if (!v1buf) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); return -1; }
              memcpy(v1buf, bpv->map + base + soff + 4, len);
              v1 = v1buf;
              v1sz = len;
              have_v1 = 1;
            }
          }
        }
      }
    }
    if (!have_v1) continue;   /* 从未执行 → 零填充初始,不调 transform */
    struct tx_probe *p = tx_find_probe(tx, mf_module_id(f));
    if (!p) { free(v1buf); tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx abi probe failed"); return -1; }
    u32 sz = mf_state_size(f);
    if (!sz) sz = p->abi->state_size;
    void *v2 = calloc(1, sz);   /* 零填充:transform 未覆盖的字段保持确定 */
    if (!v2) { free(v1buf); tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); return -1; }
    u32 rsv = mf_reserved(f);
    if (rsv) {
      if (rsv - 1 >= p->abi->transform_count || !p->abi->transforms) {
        free(v2); free(v1buf);
        tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx transform index out of range");
        return -1;
      }
      p->abi->transforms[rsv - 1](v1, v2, sz);
    } else {
      memcpy(v2, v1, v1sz < sz ? v1sz : sz);
    }
    free(v1buf);
    u32 off = 0;
    if (state_blob_append_pack(rt, &tx->patch, v2, sz, &off) != 0) {
      free(v2);
      tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom");
      return -1;
    }
    free(v2);
    /* I-1:append 后重取记录(mremap 可能移动补丁映射),写 state_off */
    mosaic_function_record *rw = tx_find_patch_fn(tx, fn_id);
    if (!rw) { tx_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "tx patch fn lost"); return -1; }
    mf_set_state_off(rw, off);
  }
  return 0;
}

/* quiesce v1:每个更新函数 ws_find 命中 → 墓碑(必须发生在路由切换**之前**,
   墓碑内部按 find_function_active 解析记录,切换后解析到补丁记录会失败;
   此时墓碑把 v1 state 再写一次 base blob——无害,保 v1 状态供 demote)。 */
static int tx_quiesce_v1(mosaic_tx *tx, char *errbuf, size_t errlen) {
  mosaic_runtime *rt = tx->rt;
  u64 nf = hdr_fn_count(tx->patch.map);
  for (u64 i = 0; i < nf; i++) {
    const mosaic_function_record *f =
        (const mosaic_function_record *)(tx->patch.map + hdr_fn_off(tx->patch.map)) + i;
    mosaic_fn_obj *live = ws_find(rt, mf_id(f));
    if (live && mosaic_fn_tombstone(rt, live) != 0) {
      tx_err(rt, rt->last_err ? rt->last_err : MOSAIC_ERR_ILLEGAL, errbuf, errlen, "tx quiesce failed");
      return -1;
    }
  }
  return 0;
}

int mosaic_tx_commit(mosaic_tx *tx, char *errbuf, size_t errlen) {
  if (!tx) { if (errbuf && errlen) snprintf(errbuf, errlen, "bad tx"); return -1; }
  if (tx->committed) { if (errbuf && errlen) snprintf(errbuf, errlen, "already committed"); return -1; }
  if (tx->released) { if (errbuf && errlen) snprintf(errbuf, errlen, "released"); return -1; }
  mosaic_runtime *rt = tx->rt;
  /* 0. commit 前置:validate 必须已通过(探测缓存就绪,变换/越界已校验) */
  if (tx->n_probes < hdr_module_count(tx->patch.map)) {
    tx_err(rt, MOSAIC_ERR_ABI, errbuf, errlen, "tx not validated");
    return -1;
  }
  /* 1. 新路由表 = 当前路由副本 + 补丁条目(补丁每个函数 put(fn_id, 新
     generation);既有路由保留,多次 commit 不丢失先前补丁的路由) */
  struct gen_route *nr = calloc(1, sizeof *nr);
  if (!nr) { tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom"); return -1; }
  if (rt->routes) {
    for (u64 i = 0; i < rt->routes->cap; i++) {
      u64 k = rt->routes->keys[i];
      if (k && gen_route_put(nr, k, rt->routes->gens[i]) != 0) {
        gen_route_free(nr); free(nr);
        tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom");
        return -1;
      }
    }
  }
  u64 nf = hdr_fn_count(tx->patch.map);
  const mosaic_function_record *pfns =
      (const mosaic_function_record *)(tx->patch.map + hdr_fn_off(tx->patch.map));
  for (u64 i = 0; i < nf; i++) {
    if (gen_route_put(nr, mf_id(&pfns[i]), mf_generation(&pfns[i])) != 0) {
      gen_route_free(nr); free(nr);
      tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom");
      return -1;
    }
  }
  /* 2. 状态迁移(v1 → v2,transform,写补丁 blob) */
  if (tx_migrate_state(tx, errbuf, errlen) != 0) { gen_route_free(nr); free(nr); return -1; }
  /* 3. quiesce v1(路由未切换;失败则新表已释放,已墓碑的函数保持墓碑,
      v1 状态在 base blob——无副作用,可 abort) */
  if (tx_quiesce_v1(tx, errbuf, errlen) != 0) { gen_route_free(nr); free(nr); return -1; }
  /* 4. 原子切换:新表归 rt 所有,旧表指针存入 tx(rollback demote 用) */
  tx->old_routes = gen_route_swap(&rt->routes, nr);
  /* 5. 补丁 pack 转 runtime 持有:rt->tx_packs 追加(不进范围表);失败 →
      切换还原 */
  struct pack_view *nt = realloc(rt->tx_packs, (rt->n_tx_packs + 1) * sizeof *nt);
  if (!nt) {
    struct gen_route *cur = gen_route_swap(&rt->routes, tx->old_routes);
    tx->old_routes = NULL;
    if (cur) { gen_route_free(cur); free(cur); }
    tx_err(rt, MOSAIC_ERR_NOMEM, errbuf, errlen, "oom");
    return -1;
  }
  rt->tx_packs = nt;
  rt->tx_packs[rt->n_tx_packs++] = tx->patch;
  tx->transferred = 1;
  /* M2-2b 修复(I-1):补丁每个模块的 mods 缓存条目失效——补丁 so_path 可能
     指向新 .so,缓存命中即返回旧 abi,物化会执行旧 .so 代码(实测 12 而非
     17)。失效后下次 mod_load 重新 dlopen 补丁 so_path → 新 .so 生效。 */
  {
    u64 mc = hdr_module_count(tx->patch.map);
    const mosaic_module_record *pmods =
        (const mosaic_module_record *)(tx->patch.map + hdr_module_off(tx->patch.map));
    for (u64 i = 0; i < mc; i++) mods_invalidate(rt, mm_id(&pmods[i]));
  }
  tx->committed = 1;
  tx_release_probes(tx);   /* 探测 .so 使命完成(物化走 mod_load 重新 dlopen) */
  return 0;
}

/* ---- rollback(demote) ---- */

int mosaic_tx_rollback(mosaic_tx *tx, char *errbuf, size_t errlen) {
  if (!tx) { if (errbuf && errlen) snprintf(errbuf, errlen, "bad tx"); return -1; }
  if (!tx->committed) { if (errbuf && errlen) snprintf(errbuf, errlen, "not committed"); return -1; }
  mosaic_runtime *rt = tx->rt;
  /* 路由切回旧表(首次 commit 的旧表为 NULL——之前无路由);被撤销的新表释放 */
  struct gen_route *cur = gen_route_swap(&rt->routes, tx->old_routes);
  tx->old_routes = NULL;
  if (cur) { gen_route_free(cur); free(cur); }
  /* M2 遗留修复(设计缺口):demote 后补丁模块的 mods 缓存条目仍在——物化补丁
     函数时 mod_load 命中缓存即返回补丁 .so 的旧 abi,以 base 记录 code_off
     执行补丁 .so 代码(实测本应 +2 却 +7)。与 commit 转持阶段对称:对补丁
     每个模块 mods_invalidate(旧 .so 挂 mods_dead 链延迟释放),下次 mod_load
     按 base 记录 so_path 重新 dlopen。时序:先 swap 回旧路由,再 invalidate,
     再卸载补丁 pack。 */
  {
    u64 mc = hdr_module_count(tx->patch.map);
    const mosaic_module_record *pmods =
        (const mosaic_module_record *)(tx->patch.map + hdr_module_off(tx->patch.map));
    for (u64 i = 0; i < mc; i++) mods_invalidate(rt, mm_id(&pmods[i]));
  }
  /* 补丁 pack 从 rt->tx_packs 移除并 unmap(记录在磁盘仍在,重开可再 begin) */
  for (size_t i = 0; i < rt->n_tx_packs; i++) {
    if (rt->tx_packs[i].fd == tx->patch.fd) {
      munmap(rt->tx_packs[i].map, rt->tx_packs[i].map_len);
      close(rt->tx_packs[i].fd);
      memmove(&rt->tx_packs[i], &rt->tx_packs[i + 1],
              (rt->n_tx_packs - i - 1) * sizeof *rt->tx_packs);
      rt->n_tx_packs--;
      break;
    }
  }
  tx->patch.map = NULL;
  tx->patch.fd = -1;
  tx->transferred = 0;
  tx->committed = 0;
  tx->rolled_back = 1;
  tx->released = 1;
  return 0;
}

/* ---- abort / free ---- */

/* abort:未 commit 的 tx 释放补丁 mmap + 缓存 so/abi(dlclose),无运行时
   副作用;已 commit 的 tx 是 no-op(补丁归 runtime 持有,demote 只能显式
   rollback)。abort 不释放句柄——free 是唯一释放入口。 */
void mosaic_tx_abort(mosaic_tx *tx) {
  if (!tx || tx->released || tx->committed) return;
  if (!tx->transferred && tx->patch.map) {
    munmap(tx->patch.map, tx->patch.map_len);
    if (tx->patch.fd >= 0) close(tx->patch.fd);
  }
  tx->patch.map = NULL;
  tx->patch.fd = -1;
  tx_release_probes(tx);
  tx->released = 1;
}

void mosaic_tx_free(mosaic_tx *tx) {
  if (!tx) return;
  mosaic_tx_abort(tx);          /* 释放未转持的补丁 mmap + 探测(幂等) */
  if (tx->old_routes) { gen_route_free(tx->old_routes); free(tx->old_routes); }
  free(tx->probes);
  free(tx);
}
