#include "mosaic_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_err(mosaic_runtime *rt, u32 code, char *errbuf, size_t errlen, const char *msg) {
  rt->last_err = code;
  if (errbuf && errlen) snprintf(errbuf, errlen, "%s", msg);
}

/* 校验并换算各表在 map 内的位置;失败返回 -1 并 set_err */
static int validate_layout(mosaic_runtime *rt, char *errbuf, size_t errlen) {
  const u8 *h = rt->map;
  if (hdr_magic(h) != MOSAIC_PACK_MAGIC) { set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "bad magic"); return -1; }
  if (hdr_version(h) != MOSAIC_PACK_VERSION) { set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "bad version"); return -1; }
  u64 moff = hdr_module_off(h), mc = hdr_module_count(h);
  u64 foff = hdr_fn_off(h), fc = hdr_fn_count(h);
  u64 toff = hdr_trigger_off(h), tc = hdr_trigger_count(h);
  u64 doff = hdr_dep_off(h), dc = hdr_dep_count(h);
  u64 soff = hdr_state_off(h), scap = hdr_state_cap(h), slen = hdr_state_len(h);
  u64 meoff = hdr_meta_off(h), melen = hdr_meta_len(h);
  u64 eoff = hdr_event_names_off(h), ec = hdr_event_count(h);
  /* 每表:偏移本身必须 ≤ map_len;count×size 用除法防 u64 回绕 */
  if (moff > rt->map_len || mc > (rt->map_len - moff) / MM_SIZE ||
      foff > rt->map_len || fc > (rt->map_len - foff) / FN_SIZE ||
      toff > rt->map_len || tc > (rt->map_len - toff) / MT_SIZE ||
      doff > rt->map_len || dc > (rt->map_len - doff) / MD_SIZE ||
      meoff > rt->map_len || melen > rt->map_len - meoff ||
      eoff > rt->map_len || ec > (rt->map_len - eoff) / MN_SIZE ||
      soff > rt->map_len || scap > rt->map_len - soff || slen > scap) {
    set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "offset out of bounds");
    return -1;
  }
  return 0;
}

mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen) {
  /* 修正 D-10-3:state_blob_append 需要 ftruncate 扩容文件再 mremap(否则写入
     越过文件末页 → SIGBUS);先试 O_RDWR,只读 pack 回退 O_RDONLY(此时有状态
     写入的墓碑会走 NOMEM 错误路径优雅失败,纯查询不受影响)。 */
  int fd = open(pack_path, O_RDWR);
  if (fd < 0) fd = open(pack_path, O_RDONLY);
  if (fd < 0) { if (errbuf && errlen) snprintf(errbuf, errlen, "open %s failed", pack_path); return NULL; }
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < HDR_SIZE) { close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "pack too small"); return NULL; }
  size_t len = (size_t)st.st_size;
  void *map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) { close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "mmap failed"); return NULL; }
  mosaic_runtime *rt = calloc(1, sizeof *rt);
  if (!rt) { munmap(map, len); close(fd); if (errbuf && errlen) snprintf(errbuf, errlen, "oom"); return NULL; }
  rt->fd = fd; rt->map = map; rt->map_len = len;
  rt->state_len = hdr_state_len(map);
  rt->last_err = MOSAIC_OK;
  if (validate_layout(rt, errbuf, errlen) != 0) {
    munmap(map, len); close(fd); free(rt);
    return NULL;
  }
  return rt;
}

void mosaic_runtime_close(mosaic_runtime *rt) {
  if (!rt) return;
  for (struct mod_entry *m = rt->mods; m; ) { struct mod_entry *nx = m->next; if (m->so) dlclose(m->so); free(m); m = nx; }
  for (struct slab *s = rt->slabs; s; ) { struct slab *nx = s->next; free(s->start); free(s); s = nx; }
  free(rt->ws.keys); free(rt->ws.vals);
  munmap(rt->map, rt->map_len);
  close(rt->fd);
  free(rt);
}

u32 mosaic_runtime_last_error(const mosaic_runtime *rt) { return rt ? rt->last_err : MOSAIC_ERR_IO; }
u64 mosaic_runtime_function_count(const mosaic_runtime *rt) { return rt ? hdr_fn_count(rt->map) : 0; }

const char *mosaic_runtime_module_string(const mosaic_runtime *rt, const mosaic_module_record *m, u32 off) {
  if (!rt || !m || off == 0) return NULL;
  u64 base = hdr_meta_off(rt->map);
  if (base + off >= rt->map_len) return NULL;
  return (const char *)(rt->map + base + off);
}

u32 mosaic_runtime_event_id(const mosaic_runtime *rt, const char *name) {
  if (!rt || !name) return MOSAIC_U32_NONE;
  u32 ec = hdr_event_count(rt->map);
  u64 eoff = hdr_event_names_off(rt->map);
  u64 base = hdr_meta_off(rt->map);
  for (u32 i = 0; i < ec; i++) {
    const mosaic_event_name *en = (const mosaic_event_name *)(rt->map + eoff + (u64)i * MN_SIZE);
    u32 o = mn_off(en), l = mn_len(en);
    const char *p = (const char *)(rt->map + base + o);
    /* strcmp 会越过 NUL 窗口读到 map 外;改 strncmp 限定窗口内比较,并确认窗口尾为 NUL */
    if (base + o + l + 1 <= rt->map_len && strncmp(name, p, l) == 0 && p[l] == '\0')
      return i;
  }
  return MOSAIC_U32_NONE;
}
