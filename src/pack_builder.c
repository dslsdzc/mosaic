#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>   /* fsync */

struct mosaic_pack_builder {
  char *path;
  u64 module_count, fn_count, trigger_count, dep_count;
  u32 event_count;
  u64 mod_cursor, fn_cursor, trig_cursor, dep_cursor;
  mosaic_module_record *mods;
  mosaic_function_record *fns;
  mosaic_trigger_entry *triggers;
  mosaic_dep_entry *deps;
  mosaic_event_name *event_names;
  char *meta; size_t meta_len, meta_cap;
  int failed;
};

static void builder_err(char *errbuf, size_t errlen, const char *fmt, ...) {
  if (!errbuf || errlen == 0) return;
  va_list ap; va_start(ap, fmt);
  vsnprintf(errbuf, errlen, fmt, ap);
  va_end(ap);
}

static char *meta_add(mosaic_pack_builder *b, const char *s) {
  size_t n = strlen(s) + 1;
  if (b->meta_len + n > b->meta_cap) {
    size_t cap = b->meta_cap ? b->meta_cap * 2 : 4096;
    while (cap < b->meta_len + n) cap *= 2;
    char *m = realloc(b->meta, cap);
    if (!m) { b->failed = 1; return NULL; }
    b->meta = m; b->meta_cap = cap;
  }
  char *p = b->meta + b->meta_len;
  memcpy(p, s, n);
  b->meta_len += n;
  return p;
}

mosaic_pack_builder *mosaic_pack_builder_create(const char *path, u64 module_count, u64 fn_count,
                                                u64 trigger_count, u64 dep_count, u32 event_count) {
  mosaic_pack_builder *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->path = strdup(path);
  b->module_count = module_count; b->fn_count = fn_count;
  b->trigger_count = trigger_count; b->dep_count = dep_count;
  b->event_count = event_count;
  if (module_count) b->mods = calloc(module_count, sizeof *b->mods);
  if (fn_count) b->fns = calloc(fn_count, sizeof *b->fns);
  if (trigger_count) b->triggers = calloc(trigger_count, sizeof *b->triggers);
  if (dep_count) b->deps = calloc(dep_count, sizeof *b->deps);
  if (event_count) b->event_names = calloc(event_count, sizeof *b->event_names);
  return b;
}

void mosaic_pack_builder_add_event(mosaic_pack_builder *b, const char *name) {
  if (!b || b->failed) return;
  char *p = meta_add(b, name);
  if (!p) return;
  mn_set(&b->event_names[b->event_count - 1], (u32)(p - b->meta), (u32)strlen(name));
}

void mosaic_pack_builder_add_module(mosaic_pack_builder *b, u64 module_id, u32 version,
                                    const char *name, const char *so_path) {
  if (!b || b->failed) return;
  if (b->mod_cursor >= b->module_count) { b->failed = 1; return; }
  mosaic_module_record *m = &b->mods[b->mod_cursor++];
  mm_set_id(m, module_id);
  mm_set_version(m, version);
  mm_set_generation(m, 1);
  char *n = meta_add(b, name), *so = meta_add(b, so_path);
  if (!n || !so) return;
  mm_set_name_off(m, (u32)(n - b->meta));
  mm_set_so_off(m, (u32)(so - b->meta));
  mm_set_dep_off(m, MOSAIC_DEP_NONE);
}

void mosaic_pack_builder_add_fn(mosaic_pack_builder *b, u64 module_id, u64 local_id, u32 code_off,
                                u32 state_size, u32 generation, u32 cost_hint, u16 flags_extra) {
  if (!b || b->failed) return;
  if (b->fn_cursor >= b->fn_count) { b->failed = 1; return; }
  mosaic_function_record *f = &b->fns[b->fn_cursor++];
  mf_set_id(f, (module_id << 32) | (local_id & 0xFFFFFFFFull));
  mf_set_module_id(f, (u32)module_id);
  mf_set_code_off(f, code_off);
  mf_set_state_off(f, 0);
  mf_set_meta_off(f, 0);
  mf_set_flags(f, (u16)(MOSAIC_FN_STATE_COLD | flags_extra));
  mf_set_generation(f, generation);
  mf_set_state_size(f, state_size);
  mf_set_cost_hint(f, cost_hint);
}

void mosaic_pack_builder_add_trigger(mosaic_pack_builder *b, u32 event_id, u64 fn_id) {
  if (!b || b->failed) return;
  if (b->trig_cursor >= b->trigger_count) { b->failed = 1; return; }
  mosaic_trigger_entry *t = &b->triggers[b->trig_cursor++];
  mt_set_event(t, event_id);
  mt_set_fn(t, fn_id);
}

void mosaic_pack_builder_add_dep(mosaic_pack_builder *b, u64 owner_id, u64 dep_id) {
  if (!b || b->failed) return;
  if (b->dep_cursor >= b->dep_count) { b->failed = 1; return; }
  mosaic_dep_entry *d = &b->deps[b->dep_cursor++];
  md_set_owner(d, owner_id);
  md_set_dep(d, dep_id);
}

static int cmp_fn(const void *a, const void *b_) {
  const mosaic_function_record *x = a, *y = b_;
  u64 ix = mf_id(x), iy = mf_id(y);
  return ix < iy ? -1 : (ix > iy ? 1 : 0);
}
static int cmp_mod(const void *a, const void *b_) {
  const mosaic_module_record *x = a, *y = b_;
  u64 ix = mm_id(x), iy = mm_id(y);
  return ix < iy ? -1 : (ix > iy ? 1 : 0);
}
static int cmp_trig(const void *a, const void *b_) {
  const mosaic_trigger_entry *x = a, *y = b_;
  u32 ex = mt_event_id(x), ey = mt_event_id(y);
  if (ex != ey) return ex < ey ? -1 : 1;
  u64 fx = mt_fn_id(x), fy = mt_fn_id(y);
  return fx < fy ? -1 : (fx > fy ? 1 : 0);
}
static int cmp_dep(const void *a, const void *b_) {
  const mosaic_dep_entry *x = a, *y = b_;
  u64 ox = md_owner_id(x), oy = md_owner_id(y);
  return ox < oy ? -1 : (ox > oy ? 1 : 0);
}

static int check_dupes(const mosaic_function_record *fns, u64 n, char *err, size_t errlen) {
  for (u64 i = 1; i < n; i++)
    if (mf_id(&fns[i]) == mf_id(&fns[i - 1])) {
      builder_err(err, errlen, "duplicate function id %llu", (unsigned long long)mf_id(&fns[i]));
      return -1;
    }
  return 0;
}

int mosaic_pack_builder_finish(mosaic_pack_builder *b, char *errbuf, size_t errlen) {
  if (!b) { builder_err(errbuf, errlen, "null builder"); return -1; }
  if (b->failed || b->mod_cursor != b->module_count || b->fn_cursor != b->fn_count ||
      b->trig_cursor != b->trigger_count || b->dep_cursor != b->dep_count) {
    builder_err(errbuf, errlen, "record count mismatch (fill before finish)");
    return -1;
  }
  if (b->event_count > 64) { builder_err(errbuf, errlen, "too many events (>64)"); return -1; }

  qsort(b->mods, (size_t)b->module_count, sizeof *b->mods, cmp_mod);
  qsort(b->fns, (size_t)b->fn_count, sizeof *b->fns, cmp_fn);
  qsort(b->triggers, (size_t)b->trigger_count, sizeof *b->triggers, cmp_trig);
  qsort(b->deps, (size_t)b->dep_count, sizeof *b->deps, cmp_dep);
  if (check_dupes(b->fns, b->fn_count, errbuf, errlen)) return -1;
  for (u64 i = 1; i < b->module_count; i++)
    if (mm_id(&b->mods[i]) == mm_id(&b->mods[i - 1])) {
      builder_err(errbuf, errlen, "duplicate module id %llu", (unsigned long long)mm_id(&b->mods[i]));
      return -1;
    }

  /* 修正 fn_base:遍历已排序函数表,记录每模块首个函数的下标 */
  u64 fi = 0;
  for (u64 mi = 0; mi < b->module_count && fi < b->fn_count; mi++) {
    u64 mid = mm_id(&b->mods[mi]);
    if (mf_module_id(&b->fns[fi]) == mid) {
      mm_set_fn_base(&b->mods[mi], (u32)fi);
      u64 start = fi;
      while (fi < b->fn_count && mf_module_id(&b->fns[fi]) == mid) fi++;
      mm_set_fn_count(&b->mods[mi], (u32)(fi - start));
    }
  }
  /* 修正 dep_off:遍历已排序依赖表 */
  u64 di = 0;
  for (u64 mi = 0; mi < b->module_count && di < b->dep_count; mi++) {
    u64 mid = mm_id(&b->mods[mi]);
    if (md_owner_id(&b->deps[di]) == mid) mm_set_dep_off(&b->mods[mi], (u32)di);
  }

  u64 off_mods = HDR_SIZE;
  u64 off_fns = off_mods + (u64)b->module_count * MM_SIZE;
  u64 off_trig = off_fns + (u64)b->fn_count * FN_SIZE;
  u64 off_deps = off_trig + (u64)b->trigger_count * MT_SIZE;
  u64 off_meta = off_deps + (u64)b->dep_count * MD_SIZE;
  u64 off_events = off_meta + (u64)b->meta_len;
  u64 off_state = off_events + (u64)b->event_count * MN_SIZE;
  u64 file_size = off_state; /* 状态 blob 初始为空 */

  FILE *f = fopen(b->path, "wb");
  if (!f) { builder_err(errbuf, errlen, "cannot open %s", b->path); return -1; }
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, MOSAIC_PACK_VERSION);
  hdr_set_module_count(hdr, b->module_count);
  hdr_set_fn_count(hdr, b->fn_count);
  hdr_set_trigger_count(hdr, b->trigger_count);
  hdr_set_dep_count(hdr, b->dep_count);
  hdr_set_module_off(hdr, off_mods); hdr_set_fn_off(hdr, off_fns);
  hdr_set_trigger_off(hdr, off_trig); hdr_set_dep_off(hdr, off_deps);
  hdr_set_meta_off(hdr, off_meta); hdr_set_meta_len(hdr, (u64)b->meta_len);
  hdr_set_event_count(hdr, b->event_count); hdr_set_event_names_off(hdr, off_events);
  hdr_set_state_off(hdr, off_state); hdr_set_state_cap(hdr, 0); hdr_set_state_len(hdr, 0);

  int rc = 0;
  if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr) rc = -1;
  if (!rc && b->module_count && fwrite(b->mods, sizeof *b->mods, (size_t)b->module_count, f) != b->module_count) rc = -1;
  if (!rc && b->fn_count && fwrite(b->fns, sizeof *b->fns, (size_t)b->fn_count, f) != b->fn_count) rc = -1;
  if (!rc && b->trigger_count && fwrite(b->triggers, sizeof *b->triggers, (size_t)b->trigger_count, f) != b->trigger_count) rc = -1;
  if (!rc && b->dep_count && fwrite(b->deps, sizeof *b->deps, (size_t)b->dep_count, f) != b->dep_count) rc = -1;
  if (!rc && b->meta_len && fwrite(b->meta, 1, b->meta_len, f) != b->meta_len) rc = -1;
  if (!rc && b->event_count && fwrite(b->event_names, sizeof *b->event_names, (size_t)b->event_count, f) != b->event_count) rc = -1;
  if (!rc) {
    if (fflush(f) != 0) rc = -1;
    if (fsync(fileno(f)) != 0) rc = -1;
  }
  if (rc) builder_err(errbuf, errlen, "write failed");
  fclose(f);
  return rc;
}

void mosaic_pack_builder_free(mosaic_pack_builder *b) {
  if (!b) return;
  free(b->path); free(b->mods); free(b->fns); free(b->triggers);
  free(b->deps); free(b->event_names); free(b->meta); free(b);
}
