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
  struct mod_entry *next;
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

struct mosaic_runtime {
  int fd;
  u8 *map;
  size_t map_len;
  u64 state_len;           /* 状态 blob 追加游标 */
  struct mod_entry *mods;  /* 已 dlopen 的模块 */
  struct ws_hash ws;
  struct mosaic_fn_obj *ws_head, *ws_tail;
  struct slab *slabs;
  u32 last_err;
};

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

/* lifecycle.c */
const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id);
void mod_unload(mosaic_runtime *rt, u64 module_id);
int state_blob_append(mosaic_runtime *rt, const void *bytes, u32 len, u32 *out_off);
#endif
