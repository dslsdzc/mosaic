#ifndef MOSAIC_PACK_H
#define MOSAIC_PACK_H
#include "mosaic/base.h"

#define MOSAIC_PACK_MAGIC   0x41534F4Du  /* "MOSA" LE */
#define MOSAIC_PACK_VERSION 1

/* ---- Pack header (256B, LE) ---- */
enum {
  HDR_MAGIC = 0, HDR_VERSION = 4,
  HDR_MODULE_COUNT = 8, HDR_FN_COUNT = 16, HDR_TRIGGER_COUNT = 24, HDR_DEP_COUNT = 32,
  HDR_MODULE_OFF = 40, HDR_FN_OFF = 48, HDR_TRIGGER_OFF = 56, HDR_DEP_OFF = 64,
  HDR_STATE_OFF = 72, HDR_STATE_CAP = 80, HDR_STATE_LEN = 88,
  HDR_META_OFF = 96, HDR_META_LEN = 104,
  HDR_EVENT_COUNT = 112, HDR_EVENT_NAMES_OFF = 120,
  HDR_SIZE = 256
};
static inline u32 hdr_magic(const u8 *h) { return rd_le32(h + HDR_MAGIC); }
static inline u32 hdr_version(const u8 *h) { return rd_le32(h + HDR_VERSION); }
static inline u64 hdr_module_count(const u8 *h) { return rd_le64(h + HDR_MODULE_COUNT); }
static inline u64 hdr_fn_count(const u8 *h) { return rd_le64(h + HDR_FN_COUNT); }
static inline u64 hdr_trigger_count(const u8 *h) { return rd_le64(h + HDR_TRIGGER_COUNT); }
static inline u64 hdr_dep_count(const u8 *h) { return rd_le64(h + HDR_DEP_COUNT); }
static inline u64 hdr_module_off(const u8 *h) { return rd_le64(h + HDR_MODULE_OFF); }
static inline u64 hdr_fn_off(const u8 *h) { return rd_le64(h + HDR_FN_OFF); }
static inline u64 hdr_trigger_off(const u8 *h) { return rd_le64(h + HDR_TRIGGER_OFF); }
static inline u64 hdr_dep_off(const u8 *h) { return rd_le64(h + HDR_DEP_OFF); }
static inline u64 hdr_state_off(const u8 *h) { return rd_le64(h + HDR_STATE_OFF); }
static inline u64 hdr_state_cap(const u8 *h) { return rd_le64(h + HDR_STATE_CAP); }
static inline u64 hdr_state_len(const u8 *h) { return rd_le64(h + HDR_STATE_LEN); }
static inline u64 hdr_meta_off(const u8 *h) { return rd_le64(h + HDR_META_OFF); }
static inline u64 hdr_meta_len(const u8 *h) { return rd_le64(h + HDR_META_LEN); }
static inline u32 hdr_event_count(const u8 *h) { return rd_le32(h + HDR_EVENT_COUNT); }
static inline u64 hdr_event_names_off(const u8 *h) { return rd_le64(h + HDR_EVENT_NAMES_OFF); }
static inline void hdr_set_module_count(u8 *h, u64 v) { wr_le64(h + HDR_MODULE_COUNT, v); }
static inline void hdr_set_fn_count(u8 *h, u64 v) { wr_le64(h + HDR_FN_COUNT, v); }
static inline void hdr_set_trigger_count(u8 *h, u64 v) { wr_le64(h + HDR_TRIGGER_COUNT, v); }
static inline void hdr_set_dep_count(u8 *h, u64 v) { wr_le64(h + HDR_DEP_COUNT, v); }
static inline void hdr_set_module_off(u8 *h, u64 v) { wr_le64(h + HDR_MODULE_OFF, v); }
static inline void hdr_set_fn_off(u8 *h, u64 v) { wr_le64(h + HDR_FN_OFF, v); }
static inline void hdr_set_trigger_off(u8 *h, u64 v) { wr_le64(h + HDR_TRIGGER_OFF, v); }
static inline void hdr_set_dep_off(u8 *h, u64 v) { wr_le64(h + HDR_DEP_OFF, v); }
static inline void hdr_set_state_off(u8 *h, u64 v) { wr_le64(h + HDR_STATE_OFF, v); }
static inline void hdr_set_state_cap(u8 *h, u64 v) { wr_le64(h + HDR_STATE_CAP, v); }
static inline void hdr_set_state_len(u8 *h, u64 v) { wr_le64(h + HDR_STATE_LEN, v); }
static inline void hdr_set_meta_off(u8 *h, u64 v) { wr_le64(h + HDR_META_OFF, v); }
static inline void hdr_set_meta_len(u8 *h, u64 v) { wr_le64(h + HDR_META_LEN, v); }
static inline void hdr_set_event_count(u8 *h, u32 v) { wr_le32(h + HDR_EVENT_COUNT, v); }
static inline void hdr_set_event_names_off(u8 *h, u64 v) { wr_le64(h + HDR_EVENT_NAMES_OFF, v); }

/* ---- FunctionRecord (48B) ---- */
enum {
  FN_OFF_CODE = 0, FN_OFF_DEP = 4, FN_OFF_STATE = 8, FN_OFF_META = 12,
  FN_OFF_ID = 16, FN_OFF_MODULE = 24, FN_OFF_FLAGS = 28, FN_OFF_PAD = 30,
  FN_OFF_GEN = 32, FN_OFF_SIZE_HINT = 36, FN_OFF_COST = 40, FN_OFF_RSVD = 44,
  FN_SIZE = 48
};
typedef struct { u8 bytes[FN_SIZE]; } mosaic_function_record;

/* flags 位域 */
#define MOSAIC_FN_STATE_MASK       0x3u
#define MOSAIC_FN_STATE_COLD       0x0u   /* 从未物化;+state_off≠0 即 TOMBSTONED */
#define MOSAIC_FN_STATE_MATERIALIZING 0x1u
#define MOSAIC_FN_STATE_ACTIVE     0x2u
#define MOSAIC_FN_STATE_QUIESCING  0x3u
#define MOSAIC_FN_REQUIRES_STATE   0x4u
#define MOSAIC_FN_TOMBSTONE_ABLE   0x8u
#define MOSAIC_KA_MASK             0xC0u
#define MOSAIC_KA_WARM             0x00u
#define MOSAIC_KA_COLD             0x40u
#define MOSAIC_KA_COLDEST          0x80u

static inline u32 mf_code_off(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_CODE); }
static inline u32 mf_dep_off(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_DEP); }
static inline u32 mf_state_off(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_STATE); }
static inline u32 mf_meta_off(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_META); }
static inline u64 mf_id(const mosaic_function_record *r) { return rd_le64(r->bytes + FN_OFF_ID); }
static inline u32 mf_module_id(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_MODULE); }
static inline u16 mf_flags(const mosaic_function_record *r) { return rd_le16(r->bytes + FN_OFF_FLAGS); }
static inline u32 mf_generation(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_GEN); }
static inline u32 mf_state_size(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_SIZE_HINT); }
static inline u32 mf_cost_hint(const mosaic_function_record *r) { return rd_le32(r->bytes + FN_OFF_COST); }
static inline void mf_set_code_off(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_CODE, v); }
static inline void mf_set_state_off(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_STATE, v); }
static inline void mf_set_meta_off(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_META, v); }
static inline void mf_set_id(mosaic_function_record *r, u64 v) { wr_le64(r->bytes + FN_OFF_ID, v); }
static inline void mf_set_module_id(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_MODULE, v); }
static inline void mf_set_flags(mosaic_function_record *r, u16 v) { wr_le16(r->bytes + FN_OFF_FLAGS, v); }
static inline void mf_set_generation(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_GEN, v); }
static inline void mf_set_state_size(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_SIZE_HINT, v); }
static inline void mf_set_cost_hint(mosaic_function_record *r, u32 v) { wr_le32(r->bytes + FN_OFF_COST, v); }

/* ---- ModuleRecord (64B) ---- */
enum {
  MM_OFF_ID = 0, MM_OFF_FN_BASE = 8, MM_OFF_FN_COUNT = 12, MM_OFF_DEP = 16,
  MM_OFF_NAME = 20, MM_OFF_SO = 24, MM_OFF_VERSION = 28, MM_OFF_GEN = 32,
  MM_OFF_FLAGS = 36, MM_OFF_PAD = 38, MM_OFF_RSVD = 40,
  MM_SIZE = 64
};
typedef struct { u8 bytes[MM_SIZE]; } mosaic_module_record;
#define MOSAIC_DEP_NONE MOSAIC_U32_NONE

static inline u64 mm_id(const mosaic_module_record *m) { return rd_le64(m->bytes + MM_OFF_ID); }
static inline u32 mm_fn_base(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_FN_BASE); }
static inline u32 mm_fn_count(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_FN_COUNT); }
static inline u32 mm_dep_off(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_DEP); }
static inline u32 mm_name_off(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_NAME); }
static inline u32 mm_so_off(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_SO); }
static inline u32 mm_version(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_VERSION); }
static inline u32 mm_generation(const mosaic_module_record *m) { return rd_le32(m->bytes + MM_OFF_GEN); }
static inline u16 mm_flags(const mosaic_module_record *m) { return rd_le16(m->bytes + MM_OFF_FLAGS); }
static inline void mm_set_id(mosaic_module_record *m, u64 v) { wr_le64(m->bytes + MM_OFF_ID, v); }
static inline void mm_set_fn_base(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_FN_BASE, v); }
static inline void mm_set_fn_count(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_FN_COUNT, v); }
static inline void mm_set_dep_off(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_DEP, v); }
static inline void mm_set_name_off(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_NAME, v); }
static inline void mm_set_so_off(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_SO, v); }
static inline void mm_set_version(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_VERSION, v); }
static inline void mm_set_generation(mosaic_module_record *m, u32 v) { wr_le32(m->bytes + MM_OFF_GEN, v); }
static inline void mm_set_flags(mosaic_module_record *m, u16 v) { wr_le16(m->bytes + MM_OFF_FLAGS, v); }

/* ---- TriggerEntry (16B): (event_id, fn_id),按 (event,fn) 排序 ---- */
enum { MT_OFF_EVENT = 0, MT_OFF_FN = 8, MT_SIZE = 16 };
typedef struct { u8 bytes[MT_SIZE]; } mosaic_trigger_entry;
static inline u32 mt_event_id(const mosaic_trigger_entry *t) { return rd_le32(t->bytes + MT_OFF_EVENT); }
static inline u64 mt_fn_id(const mosaic_trigger_entry *t) { return rd_le64(t->bytes + MT_OFF_FN); }
static inline void mt_set_event(mosaic_trigger_entry *t, u32 v) { wr_le32(t->bytes + MT_OFF_EVENT, v); }
static inline void mt_set_fn(mosaic_trigger_entry *t, u64 v) { wr_le64(t->bytes + MT_OFF_FN, v); }

/* ---- DepEntry (16B): (owner_id, dep_id),按 owner 排序 ---- */
enum { MD_OFF_OWNER = 0, MD_OFF_DEP = 8, MD_SIZE = 16 };
typedef struct { u8 bytes[MD_SIZE]; } mosaic_dep_entry;
static inline u64 md_owner_id(const mosaic_dep_entry *d) { return rd_le64(d->bytes + MD_OFF_OWNER); }
static inline u64 md_dep_id(const mosaic_dep_entry *d) { return rd_le64(d->bytes + MD_OFF_DEP); }
static inline void md_set_owner(mosaic_dep_entry *d, u64 v) { wr_le64(d->bytes + MD_OFF_OWNER, v); }
static inline void md_set_dep(mosaic_dep_entry *d, u64 v) { wr_le64(d->bytes + MD_OFF_DEP, v); }

/* ---- EventName (8B): (off, len) 指向 meta blob ---- */
enum { MN_OFF_OFF = 0, MN_OFF_LEN = 4, MN_SIZE = 8 };
typedef struct { u8 bytes[MN_SIZE]; } mosaic_event_name;
static inline u32 mn_off(const mosaic_event_name *n) { return rd_le32(n->bytes + MN_OFF_OFF); }
static inline u32 mn_len(const mosaic_event_name *n) { return rd_le32(n->bytes + MN_OFF_LEN); }
static inline void mn_set(mosaic_event_name *n, u32 off, u32 len) { wr_le32(n->bytes + MN_OFF_OFF, off); wr_le32(n->bytes + MN_OFF_LEN, len); }

/* ---- Pack builder ---- */
typedef struct mosaic_pack_builder mosaic_pack_builder;

mosaic_pack_builder *mosaic_pack_builder_create(const char *path, u64 module_count, u64 fn_count,
                                                u64 trigger_count, u64 dep_count, u32 event_count);
void mosaic_pack_builder_add_event(mosaic_pack_builder *b, const char *name);
void mosaic_pack_builder_add_module(mosaic_pack_builder *b, u64 module_id, u32 version,
                                    const char *name, const char *so_path);
void mosaic_pack_builder_add_fn(mosaic_pack_builder *b, u64 module_id, u64 local_id, u32 code_off,
                                u32 state_size, u32 generation, u32 cost_hint, u16 flags_extra);
void mosaic_pack_builder_add_trigger(mosaic_pack_builder *b, u32 event_id, u64 fn_id);
void mosaic_pack_builder_add_dep(mosaic_pack_builder *b, u64 owner_id, u64 dep_id);
int mosaic_pack_builder_finish(mosaic_pack_builder *b, char *errbuf, size_t errlen);
void mosaic_pack_builder_free(mosaic_pack_builder *b);
#endif
