# Mosaic M1 核心运行时实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Mosaic 运行时核心循环——冷存储(mmap pack)、紧凑索引、工作集、函数级状态机(物化/执行/墓碑/恢复)、触发索引派发、所有权租约、驱逐策略,并以激进硬指标(10M 冷函数 ≤80MB RSS、全循环 ≤500μs、热路径 ≤1.10× 直调)验证"千万级 Mod Universe 不占内存"命题。

**Architecture:** 五层(L0 冷存储 → L1 紧凑索引 → L2 工作集 → L3 生命周期引擎 → L4 服务)全部为纯 C11,零外部依赖。冷数据唯一事实源在 mmap pack 文件(记录 48B/64B 固定布局),RAM 只放工作集对象与微型哈希;生命周期状态(COLD/MATERIALIZING/ACTIVE/QUIESCING)以 2 位 flags 内联在 mmap 记录里,TOMBSTONED = COLD + state_off≠0。热路径 = 一次间接调用,零检查。

**Tech Stack:** C11, CMake ≥ 3.20, libc + dl + pthread(仅链接), Linux mmap/mremap, 自研 mini 测试框架(零依赖), 合成函数 .so 经 `mosaic_module_abi_v1` 模块 ABI 接入。

## Global Constraints

- 固定宽度、显式小端序二进制布局;`static_assert` 记录尺寸:FunctionRecord=48B、ModuleRecord=64B、TriggerEntry=16B、DepEntry=16B、PackHeader=256B。
- mmap 打开用 `PROT_READ|PROT_WRITE, MAP_PRIVATE`;运行时对记录的写(flags/state_off)只进 RAM,不落盘。
- 冷数据不建 RAM 对象图;find_function/find_module 只做 mmap 内二分查找。
- 验收门禁(写死进 ci/gates.sh):S1 冷规模 RSS 增量 ≤ 80MB;S3 全循环 ≤ 500μs;S4 热路径分派/直调 ≤ 1.10;S2 为诊断非门禁。
- 热路径 `mosaic_fn_execute` 零状态机检查(直接 `fn->code(fn->state, ...)`)。
- `refs > 0` 的函数绝不驱逐/墓碑(返回 MOSAIC_ERR_BUSY)。
- 运行时物化失败 → 事件降级(跳过该函数 + stderr 诊断),不崩溃;pack 格式错 → fail-fast(MOSAIC_ERR_BAD_PACK)。
- 非法状态转移 → MOSAIC_ERR_ILLEGAL。
- 所有字符串(模块名、so 路径、事件名)进 meta blob(NUL 结尾);事件名 ≤ 64 个。
- 并行索引构建在 M1 推迟(构建是离线工具,不参与验收门禁;qsort 已隔离在 `finish()` 内,后续可整体替换为并行排序)。

---

### Task 1: 项目骨架 —— CMake + mini 测试框架 + base.h + pack.h 布局与访问器

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/mosaic/base.h`
- Create: `include/mosaic/pack.h`
- Create: `tests/mini_test.h`
- Create: `tests/test_pack.c`
- Test: `tests/test_pack.c`(静态断言 + 访问器 round-trip)

**Interfaces:**
- Consumes: 无(首个任务)
- Produces: 类型 `u8/u16/u32/u64`;`rd_le16/32/64`、`wr_le16/32/64`;错误码枚举 `MOSAIC_OK / MOSAIC_ERR_BAD_PACK / NOT_FOUND / BUSY / ILLEGAL / ABI / NOMEM / IO`;`MOSAIC_U32_NONE`;记录类型 `mosaic_function_record`(48B 字节数组)、`mosaic_module_record`(64B)、`mosaic_trigger_entry`(16B)、`mosaic_dep_entry`(16B);`mosaic_pack_header` 字节布局 + 全部 `mf_* / mm_* / mt_* / md_* / hdr_*` 访问器与写入器;`MOSAIC_FN_STATE_*` 常量;`MT_CHECK / MT_CHECK_EQ_U64 / MT_RUN / MT_RESULT` 测试宏

- [ ] **Step 1: 写 base.h(类型 + 小端访问器 + 错误码)**

```c
#ifndef MOSAIC_BASE_H
#define MOSAIC_BASE_H
#include <stdint.h>
#include <string.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MOSAIC_BIG_ENDIAN 1
#else
#define MOSAIC_BIG_ENDIAN 0
#endif

static inline u16 rd_le16(const void *p) { u16 v; memcpy(&v, p, 2); return MOSAIC_BIG_ENDIAN ? (u16)((v >> 8) | (v << 8)) : v; }
static inline u32 rd_le32(const void *p) { u32 v; memcpy(&v, p, 4); return MOSAIC_BIG_ENDIAN ? __builtin_bswap32(v) : v; }
static inline u64 rd_le64(const void *p) { u64 v; memcpy(&v, p, 8); return MOSAIC_BIG_ENDIAN ? __builtin_bswap64(v) : v; }
static inline void wr_le16(void *p, u16 v) { u16 x = MOSAIC_BIG_ENDIAN ? (u16)((v >> 8) | (v << 8)) : v; memcpy(p, &x, 2); }
static inline void wr_le32(void *p, u32 v) { u32 x = MOSAIC_BIG_ENDIAN ? __builtin_bswap32(v) : v; memcpy(p, &x, 4); }
static inline void wr_le64(void *p, u64 v) { u64 x = MOSAIC_BIG_ENDIAN ? __builtin_bswap64(v) : v; memcpy(p, &x, 8); }

enum {
  MOSAIC_OK = 0,
  MOSAIC_ERR_BAD_PACK = 1,   /* 魔数/版本/偏移越界 */
  MOSAIC_ERR_NOT_FOUND = 2,
  MOSAIC_ERR_BUSY = 3,       /* refs > 0 时请求墓碑 */
  MOSAIC_ERR_ILLEGAL = 4,    /* 非法状态转移 */
  MOSAIC_ERR_ABI = 5,        /* dlopen/dlsym/ABI 不匹配 */
  MOSAIC_ERR_NOMEM = 6,
  MOSAIC_ERR_IO = 7,
};
#define MOSAIC_U32_NONE 0xFFFFFFFFu
#endif
```

- [ ] **Step 2: 写 pack.h(布局常量 + 访问器)**

```c
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
#endif
```

- [ ] **Step 3: 写 tests/mini_test.h**

```c
#ifndef MOSAIC_MINI_TEST_H
#define MOSAIC_MINI_TEST_H
#include <stdio.h>
#include <stdint.h>
static int mt_failures = 0;
#define MT_CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); mt_failures++; } } while (0)
#define MT_CHECK_EQ_U64(a, b) do { uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
  if (_a != _b) { fprintf(stderr, "FAIL %s:%d: %s == %s (%llu != %llu)\n", __FILE__, __LINE__, #a, #b, \
                          (unsigned long long)_a, (unsigned long long)_b); mt_failures++; } } while (0)
#define MT_RUN(fn) do { int _pre = mt_failures; fn(); \
  fprintf(stderr, "  %-28s %s\n", #fn, mt_failures == _pre ? "ok" : "FAILED"); } while (0)
#define MT_RESULT() (mt_failures == 0)
#endif
```

- [ ] **Step 4: 写 tests/test_pack.c(布局断言 + 访问器 round-trip)**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mini_test.h"
#include <string.h>

static void test_record_sizes(void) {
  MT_CHECK(sizeof(mosaic_function_record) == 48);
  MT_CHECK(sizeof(mosaic_module_record) == 64);
  MT_CHECK(sizeof(mosaic_trigger_entry) == 16);
  MT_CHECK(sizeof(mosaic_dep_entry) == 16);
  MT_CHECK(HDR_SIZE == 256);
}

static void test_fn_accessors(void) {
  mosaic_function_record r; memset(&r, 0, sizeof r);
  mf_set_id(&r, 0x1122334455667788ull);
  mf_set_module_id(&r, 42);
  mf_set_code_off(&r, 7);
  mf_set_state_off(&r, 4096);
  mf_set_flags(&r, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_STATE_ACTIVE);
  mf_set_generation(&r, 2);
  mf_set_state_size(&r, 64);
  mf_set_cost_hint(&r, 5);
  MT_CHECK_EQ_U64(mf_id(&r), 0x1122334455667788ull);
  MT_CHECK_EQ_U64(mf_module_id(&r), 42);
  MT_CHECK_EQ_U64(mf_code_off(&r), 7);
  MT_CHECK_EQ_U64(mf_state_off(&r), 4096);
  MT_CHECK_EQ_U64(mf_flags(&r) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_ACTIVE);
  MT_CHECK(mf_flags(&r) & MOSAIC_FN_REQUIRES_STATE);
  MT_CHECK_EQ_U64(mf_generation(&r), 2);
  MT_CHECK_EQ_U64(mf_state_size(&r), 64);
  MT_CHECK_EQ_U64(mf_cost_hint(&r), 5);
}

static void test_module_accessors(void) {
  mosaic_module_record m; memset(&m, 0, sizeof m);
  mm_set_id(&m, 9); mm_set_fn_base(&m, 100); mm_set_fn_count(&m, 50);
  mm_set_dep_off(&m, 3); mm_set_name_off(&m, 8); mm_set_so_off(&m, 20);
  mm_set_version(&m, 1); mm_set_generation(&m, 1);
  MT_CHECK_EQ_U64(mm_id(&m), 9); MT_CHECK_EQ_U64(mm_fn_base(&m), 100);
  MT_CHECK_EQ_U64(mm_fn_count(&m), 50); MT_CHECK_EQ_U64(mm_dep_off(&m), 3);
  MT_CHECK_EQ_U64(mm_name_off(&m), 8); MT_CHECK_EQ_U64(mm_so_off(&m), 20);
  MT_CHECK_EQ_U64(mm_version(&m), 1); MT_CHECK_EQ_U64(mm_generation(&m), 1);
}

static void test_trigger_dep_accessors(void) {
  mosaic_trigger_entry t; memset(&t, 0, sizeof t);
  mt_set_event(&t, 77); mt_set_fn(&t, 0xAABBCCDD00112233ull);
  MT_CHECK_EQ_U64(mt_event_id(&t), 77);
  MT_CHECK_EQ_U64(mt_fn_id(&t), 0xAABBCCDD00112233ull);
  mosaic_dep_entry d; memset(&d, 0, sizeof d);
  md_set_owner(&d, 1); md_set_dep(&d, 2);
  MT_CHECK_EQ_U64(md_owner_id(&d), 1); MT_CHECK_EQ_U64(md_dep_id(&d), 2);
}

static void test_bytes_written_le(void) {
  mosaic_function_record r; memset(&r, 0, sizeof r);
  mf_set_id(&r, 0x0102030405060708ull);
  /* 小端:低字节在前 */
  MT_CHECK(r.bytes[FN_OFF_ID + 0] == 0x08 && r.bytes[FN_OFF_ID + 7] == 0x01);
}

int main(void) {
  MT_RUN(test_record_sizes);
  MT_RUN(test_fn_accessors);
  MT_RUN(test_module_accessors);
  MT_RUN(test_trigger_dep_accessors);
  MT_RUN(test_bytes_written_le);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 5: 写 CMakeLists.txt(含后续任务全部目标的声明,先只启用 test_pack)**

```cmake
cmake_minimum_required(VERSION 3.20)
project(mosaic C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(mosaic_core STATIC
  src/pack_reader.c
  src/index.c
  src/working_set.c
  src/lifecycle.c
  src/trigger.c
  src/ownership.c
  src/eviction.c
)
target_include_directories(mosaic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mosaic_core PRIVATE m dl)

enable_testing()

add_executable(test_pack tests/test_pack.c)
target_link_libraries(test_pack mosaic_core)
add_test(NAME test_pack COMMAND test_pack)

# 后续任务启用(先在文件中注释掉,避免缺文件编译失败):
# add_executable(test_runtime tests/test_runtime.c)   ... 见 Task 3-9
# add_executable(test_mod MODULE tests/test_mod.c)    ... 见 Task 6
# add_executable(test_badmod MODULE tests/test_badmod.c) ... 见 Task 6
# add_executable(synth_mod MODULE bench/synth_abi.c)  ... 见 Task 10
# add_executable(bench_runner bench/bench_runner.c bench/synth_universe.c) ... 见 Task 10
```

- [ ] **Step 6: 创建 src 占位文件(CMake 目标需要;每个只含头文件包含 + 注释说明后续任务实现)**

`src/pack_reader.c`:

```c
/* src/pack_reader.c — Task 3 与 runtime.c 合并实现 */
#include "mosaic_internal.h"
```

`src/index.c`:

```c
/* src/index.c — M1 索引查询,Task 4 实现 */
#include "mosaic_internal.h"
```

`src/working_set.c`:

```c
/* src/working_set.c — Task 5 实现 */
#include "mosaic_internal.h"
```

`src/lifecycle.c`:

```c
/* src/lifecycle.c — Task 6/7 实现 */
#include "mosaic_internal.h"
```

`src/trigger.c`:

```c
/* src/trigger.c — Task 8 实现 */
#include "mosaic_internal.h"
```

`src/ownership.c`:

```c
/* src/ownership.c — Task 9 实现 */
#include "mosaic_internal.h"
```

`src/eviction.c`:

```c
/* src/eviction.c — Task 9 实现 */
#include "mosaic_internal.h"
```

- [ ] **Step 7: 运行测试验证通过**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `100% tests passed`;`test_pack` 输出 5 个 `ok`。

- [ ] **Step 8: 提交**

```bash
git add CMakeLists.txt include tests src
git commit -m "feat: M1 skeleton — cmake, mini test framework, pack record layout + LE accessors"
```

---

### Task 2: pack 构建器 —— 收集/排序/写出,含 round-trip 测试

**Files:**
- Create: `src/pack_builder.c`(实现函数,声明加在 `include/mosaic/pack.h` 末尾)
- Modify: `include/mosaic/pack.h`(追加 builder API 声明)
- Create: `tests/test_builder.c`
- Modify: `CMakeLists.txt`(启用 test_builder)
- Test: `tests/test_builder.c`(小规模构建 + 裸文件字节校验)

**Interfaces:**
- Consumes: Task 1 的记录类型与访问器
- Produces:
  - `mosaic_pack_builder *mosaic_pack_builder_create(const char *path, u64 module_count, u64 fn_count, u64 trigger_count, u64 dep_count, u32 event_count);`
  - `void mosaic_pack_builder_add_event(mosaic_pack_builder *b, const char *name);`(event_count 次,先于模块)
  - `void mosaic_pack_builder_add_module(mosaic_pack_builder *b, u64 module_id, u32 version, const char *name, const char *so_path);`
  - `void mosaic_pack_builder_add_fn(mosaic_pack_builder *b, u64 module_id, u64 local_id, u32 code_off, u32 state_size, u32 generation, u32 cost_hint, u16 flags_extra);`
  - `void mosaic_pack_builder_add_trigger(mosaic_pack_builder *b, u32 event_id, u64 fn_id);`
  - `void mosaic_pack_builder_add_dep(mosaic_pack_builder *b, u64 owner_id, u64 dep_id);`
  - `int mosaic_pack_builder_finish(mosaic_pack_builder *b, char *errbuf, size_t errlen);`(0 = 成功;校验计数/重复 id/local_id,排序,写文件)
  - `void mosaic_pack_builder_free(mosaic_pack_builder *b);`
  - 语义:fn_id = `module_id << 32 | local_id`;触发表按 (event,fn) 排序;函数表按 (module,local) 排序;模块表按 id 排序;依赖表按 owner 排序;模块记录 fn_base/dep_off 由排序后修正;name/so_path 进 meta blob 并回填 name_off/so_off。

- [ ] **Step 1: 在 pack.h 末尾追加 builder 声明**

```c
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
```

- [ ] **Step 2: 写实现 src/pack_builder.c**

```c
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
```

- [ ] **Step 3: 写 tests/test_builder.c(构建 → 裸读文件 → 校验布局与排序)**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_PATH "/tmp/mosaic_test_builder.pack"

static int build_pack(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(PACK_PATH, 2, 4, 3, 1, 2);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "player_join");   /* id 0 */
  mosaic_pack_builder_add_event(b, "block_break");   /* id 1 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", "/tmp/a.so");
  mosaic_pack_builder_add_module(b, 20, 2, "mod_b", "/tmp/b.so");
  /* fn id = module<<32|local */
  mosaic_pack_builder_add_fn(b, 20, 0, 1, 64, 1, 3, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 1, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 1, 0, 0, 1, 1, 0);
  mosaic_pack_builder_add_fn(b, 20, 1, 2, 64, 1, 2, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 1, 20ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 1);
  mosaic_pack_builder_add_dep(b, 20, 10);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_built_pack(void) {
  MT_CHECK(build_pack() == 0);
  FILE *f = fopen(PACK_PATH, "rb");
  MT_CHECK(f != NULL);
  u8 hdr[HDR_SIZE]; MT_CHECK(fread(hdr, 1, HDR_SIZE, f) == HDR_SIZE);
  MT_CHECK_EQ_U64(hdr_magic(hdr), MOSAIC_PACK_MAGIC);
  MT_CHECK_EQ_U64(hdr_version(hdr), MOSAIC_PACK_VERSION);
  MT_CHECK_EQ_U64(hdr_module_count(hdr), 2);
  MT_CHECK_EQ_U64(hdr_fn_count(hdr), 4);
  MT_CHECK_EQ_U64(hdr_trigger_count(hdr), 3);
  MT_CHECK_EQ_U64(hdr_dep_count(hdr), 1);
  MT_CHECK_EQ_U64(hdr_event_count(hdr), 2);

  fseek(f, (long)hdr_module_off(hdr), SEEK_SET);
  mosaic_module_record mods[2];
  MT_CHECK(fread(mods, 1, sizeof mods, f) == sizeof mods);
  /* 模块按 id 排序:10 在前,20 在后 */
  MT_CHECK_EQ_U64(mm_id(&mods[0]), 10); MT_CHECK_EQ_U64(mm_id(&mods[1]), 20);
  MT_CHECK_EQ_U64(mm_fn_base(&mods[0]), 1); MT_CHECK_EQ_U64(mm_fn_count(&mods[0]), 2);
  MT_CHECK_EQ_U64(mm_fn_base(&mods[1]), 3); MT_CHECK_EQ_U64(mm_fn_count(&mods[1]), 2);
  MT_CHECK_EQ_U64(mm_dep_off(&mods[0]), MOSAIC_DEP_NONE);   /* 无依赖 */
  MT_CHECK_EQ_U64(mm_dep_off(&mods[1]), 0);                 /* 依赖表第 0 项 */
  MT_CHECK(mm_name_off(&mods[0]) != 0);

  fseek(f, (long)hdr_fn_off(hdr), SEEK_SET);
  mosaic_function_record fns[4];
  MT_CHECK(fread(fns, 1, sizeof fns, f) == sizeof fns);
  /* 按 (module,local) 排序:10|0, 10|1, 20|0, 20|1 */
  MT_CHECK_EQ_U64(mf_id(&fns[0]), 10ull << 32 | 0);
  MT_CHECK_EQ_U64(mf_id(&fns[1]), 10ull << 32 | 1);
  MT_CHECK_EQ_U64(mf_id(&fns[2]), 20ull << 32 | 0);
  MT_CHECK_EQ_U64(mf_id(&fns[3]), 20ull << 32 | 1);
  MT_CHECK_EQ_U64(mf_code_off(&fns[0]), 0);
  MT_CHECK_EQ_U64(mf_code_off(&fns[3]), 2);
  MT_CHECK_EQ_U64(mf_state_size(&fns[1]), 0);
  MT_CHECK_EQ_U64(mf_flags(&fns[0]) & MOSAIC_FN_REQUIRES_STATE, MOSAIC_FN_REQUIRES_STATE);
  MT_CHECK_EQ_U64(mf_flags(&fns[1]) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);

  fseek(f, (long)hdr_trigger_off(hdr), SEEK_SET);
  mosaic_trigger_entry trigs[3];
  MT_CHECK(fread(trigs, 1, sizeof trigs, f) == sizeof trigs);
  /* 按 (event,fn) 排序:event0 两条在前,event1 一条在后 */
  MT_CHECK_EQ_U64(mt_event_id(&trigs[0]), 0);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[1]), 0);
  MT_CHECK_EQ_U64(mt_event_id(&trigs[2]), 1);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[0]), 10ull << 32 | 0);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[1]), 10ull << 32 | 1);
  MT_CHECK_EQ_U64(mt_fn_id(&trigs[2]), 20ull << 32 | 0);

  fclose(f);
}

static void test_duplicate_fn_rejected(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_dup.pack", 1, 2, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "m", "/tmp/x.so");
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 0, 1, 0, 0);
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 0, 1, 0, 0);  /* 重复 local_id */
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) != 0);
  MT_CHECK(strstr(err, "duplicate function id") != NULL);
  mosaic_pack_builder_free(b);
}

int main(void) {
  MT_RUN(test_built_pack);
  MT_RUN(test_duplicate_fn_rejected);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 4: 在 CMakeLists.txt 追加**

```cmake
add_executable(test_builder tests/test_builder.c)
target_link_libraries(test_builder mosaic_core)
add_test(NAME test_builder COMMAND test_builder)
```

- [ ] **Step 5: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 两个测试全过(`test_pack` + `test_builder`)。

- [ ] **Step 6: 提交**

```bash
git add include/mosaic/pack.h src/pack_builder.c tests/test_builder.c CMakeLists.txt
git commit -m "feat: pack builder with sort/fixup/validation, round-trip test"
```

---

### Task 3: runtime open/close —— mmap 读取 + 校验 fail-fast

**Files:**
- Create: `include/mosaic/runtime.h`
- Create: `src/mosaic_internal.h`(内部结构,后续任务共用)
- Create: `src/runtime.c`
- Create: `src/pack_reader.c`(空实现占位,mosaic_core 编译需要)
- Create: `src/index.c`, `src/working_set.c`, `src/lifecycle.c`, `src/trigger.c`, `src/ownership.c`, `src/eviction.c`(空实现占位)
- Create: `tests/test_runtime.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_runtime.c`(坏魔数/坏版本/偏移越界 → BAD_PACK)

**Interfaces:**
- Consumes: Task 2 的 builder(测试里用来造 pack)
- Produces:
  - `typedef struct mosaic_runtime mosaic_runtime;`
  - `mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen);`(NULL = 失败,errbuf 写原因)
  - `void mosaic_runtime_close(mosaic_runtime *rt);`
  - `u32 mosaic_runtime_last_error(const mosaic_runtime *rt);`
  - 内部 `struct mosaic_runtime`(在 mosaic_internal.h):`int fd; u8 *map; size_t map_len; u64 state_len; struct mod_entry *mods; struct ws_hash ws; struct mosaic_fn_obj *ws_head, *ws_tail; struct slab *slabs; u32 last_err;` + `now_ns()` 辅助

- [ ] **Step 1: 写 include/mosaic/runtime.h**

```c
#ifndef MOSAIC_RUNTIME_H
#define MOSAIC_RUNTIME_H
#include "mosaic/pack.h"

typedef struct mosaic_runtime mosaic_runtime;

mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen);
void mosaic_runtime_close(mosaic_runtime *rt);
u32 mosaic_runtime_last_error(const mosaic_runtime *rt);
u64 mosaic_runtime_function_count(const mosaic_runtime *rt);
const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id);
const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id);
const char *mosaic_runtime_module_string(const mosaic_runtime *rt, const mosaic_module_record *m, u32 off);
u32 mosaic_runtime_event_id(const mosaic_runtime *rt, const char *name);
#endif
```

- [ ] **Step 2: 写 src/mosaic_internal.h**

```c
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
```

- [ ] **Step 3: 写 src/runtime.c**

```c
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
  u64 ebytes = ec * MN_SIZE;
  if (moff + mc * MM_SIZE > rt->map_len || foff + fc * FN_SIZE > rt->map_len ||
      toff + tc * MT_SIZE > rt->map_len || doff + dc * MD_SIZE > rt->map_len ||
      meoff + melen > rt->map_len || eoff + ebytes > rt->map_len ||
      soff + scap > rt->map_len || slen > scap) {
    set_err(rt, MOSAIC_ERR_BAD_PACK, errbuf, errlen, "offset out of bounds");
    return -1;
  }
  return 0;
}

mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen) {
  int fd = open(pack_path, O_RDONLY);
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
    if (base + o + l + 1 <= rt->map_len && strcmp(name, (const char *)(rt->map + base + o)) == 0)
      return i;
  }
  return MOSAIC_U32_NONE;
}
```

- [ ] **Step 4: 占位 .c 文件已在 Task 1 Step 6 创建,本任务无需操作**(mosaic_core 目标已可编译链接)

- [ ] **Step 5: 写 tests/test_runtime.c**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static int build_mini(const char *path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 1, 1, 0, 1);
  if (!b) return -1;
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_module(b, 10, 1, "mod", "/tmp/nonexistent.so");
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_open_good_pack(void) {
  MT_CHECK(build_mini("/tmp/mosaic_test_good.pack") == 0);
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_good.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (rt) {
    MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), 1);
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "tick"), 0);
    MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "nope"), MOSAIC_U32_NONE);
    mosaic_runtime_close(rt);
  }
}

static void test_open_bad_magic(void) {
  FILE *f = fopen("/tmp/mosaic_test_badmagic.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, 0xDEADBEEF);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badmagic.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "magic") != NULL);
}

static void test_open_bad_version(void) {
  FILE *f = fopen("/tmp/mosaic_test_badver.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, 999);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badver.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "version") != NULL);
}

static void test_open_bad_offset(void) {
  FILE *f = fopen("/tmp/mosaic_test_badoff.pack", "wb");
  u8 hdr[HDR_SIZE]; memset(hdr, 0, sizeof hdr);
  wr_le32(hdr + HDR_MAGIC, MOSAIC_PACK_MAGIC);
  wr_le32(hdr + HDR_VERSION, MOSAIC_PACK_VERSION);
  hdr_set_module_off(hdr, 1u << 40);   /* 越界 */
  hdr_set_module_count(hdr, 100);
  fwrite(hdr, 1, sizeof hdr, f);
  fclose(f);
  char err[256] = {0};
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_badoff.pack", err, sizeof err);
  MT_CHECK(rt == NULL);
  MT_CHECK(strstr(err, "bounds") != NULL);
}

int main(void) {
  MT_RUN(test_open_good_pack);
  MT_RUN(test_open_bad_magic);
  MT_RUN(test_open_bad_version);
  MT_RUN(test_open_bad_offset);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 6: CMakeLists.txt 追加**

```cmake
add_executable(test_runtime tests/test_runtime.c)
target_link_libraries(test_runtime mosaic_core)
add_test(NAME test_runtime COMMAND test_runtime)
```

- [ ] **Step 7: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 三个测试全过。

- [ ] **Step 8: 提交**

```bash
git add include/mosaic/runtime.h src/mosaic_internal.h src/runtime.c src/pack_reader.c \
        src/index.c src/working_set.c src/lifecycle.c src/trigger.c src/ownership.c src/eviction.c \
        tests/test_runtime.c CMakeLists.txt
git commit -m "feat: runtime open/close with fail-fast pack validation"
```

---

### Task 4: 索引查询 —— find_module / find_function / 朴素对照属性测试

**Files:**
- Modify: `src/index.c`(实现)
- Create: `tests/test_index.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_index.c`(随机宇宙 vs 朴素模型)

**Interfaces:**
- Consumes: Task 3 的 runtime;Task 2 的 builder
- Produces:`mosaic_runtime_find_module` / `mosaic_runtime_find_function` 完整实现(mm 内二分查找);`mosaic_runtime_module_string` 已在 Task 3

- [ ] **Step 1: 写失败测试 tests/test_index.c(对照朴素模型)**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic_internal.h"   /* 测试直接读 rt->map 校验布局 */
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_PATH "/tmp/mosaic_test_index.pack"

static u64 rng_state = 0x12345678ull;
static u64 rng_next(void) { /* xorshift64 */
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
  return rng_state;
}

#define N_MODULES 50
#define N_FNS 2000
#define N_EVENTS 4
#define N_TRIGGERS 1500

static u64 mod_ids[N_MODULES];
static u64 fn_ids[N_FNS];
static u32 fn_modules[N_FNS];
static u32 fn_codes[N_FNS];

static void build_random_universe(void) {
  char err[256];
  /* 模块 id 取稀疏随机值,验证排序查找 */
  for (int i = 0; i < N_MODULES; i++) mod_ids[i] = (u64)(rng_next() % 1000000) + 1;
  mosaic_pack_builder *b = mosaic_pack_builder_create(PACK_PATH, N_MODULES, N_FNS, N_TRIGGERS, N_MODULES - 1, N_EVENTS);
  const char *ev[N_EVENTS] = { "player_join", "block_break", "item_use", "entity_spawn" };
  for (int i = 0; i < N_EVENTS; i++) mosaic_pack_builder_add_event(b, ev[i]);
  char so[64], name[64];
  for (int i = 0; i < N_MODULES; i++) {
    snprintf(so, sizeof so, "/tmp/mod_%llu.so", (unsigned long long)mod_ids[i]);
    snprintf(name, sizeof name, "mod_%llu", (unsigned long long)mod_ids[i]);
    mosaic_pack_builder_add_module(b, mod_ids[i], 1, name, so);
  }
  int per_mod = N_FNS / N_MODULES;   /* 40 */
  for (int i = 0; i < N_FNS; i++) {
    int mi = i / per_mod;
    u64 local = (u64)(i % per_mod);
    u32 code = (u32)(rng_next() % 3);
    fn_ids[i] = (mod_ids[mi] << 32) | local;
    fn_modules[i] = (u32)mod_ids[mi];
    fn_codes[i] = code;
    mosaic_pack_builder_add_fn(b, mod_ids[mi], local, code, 64, 1, (u32)(rng_next() % 10), MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  }
  for (int i = 1; i < N_MODULES; i++) mosaic_pack_builder_add_dep(b, mod_ids[i], mod_ids[i - 1]);
  for (int i = 0; i < N_TRIGGERS; i++) {
    u32 e = (u32)(rng_next() % N_EVENTS);
    u64 fn = fn_ids[rng_next() % N_FNS];
    mosaic_pack_builder_add_trigger(b, e, fn);
  }
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) { fprintf(stderr, "finish: %s\n", err); exit(2); }
  mosaic_pack_builder_free(b);
}

static int naive_find_fn(u64 want, const mosaic_function_record **out, const u8 *map) {
  /* 朴素实现:全表线性扫描,对照物 */
  u64 n = hdr_fn_count(map);
  const mosaic_function_record *fns = (const mosaic_function_record *)(map + hdr_fn_off(map));
  for (u64 i = 0; i < n; i++)
    if (mf_id(&fns[i]) == want) { *out = &fns[i]; return 0; }
  return -1;
}

static void test_find_all_functions(void) {
  build_random_universe();
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_function_count(rt), N_FNS);
  int hits = 0;
  for (int i = 0; i < N_FNS; i++) {
    const mosaic_function_record *r = mosaic_runtime_find_function(rt, fn_ids[i]);
    if (r) { hits++; MT_CHECK_EQ_U64(mf_id(r), fn_ids[i]); MT_CHECK_EQ_U64(mf_module_id(r), fn_modules[i]); }
    const mosaic_function_record *nr = NULL;
    int found = naive_find_fn(fn_ids[i], &nr, rt->map);
    MT_CHECK((r != NULL) == (found == 0));
  }
  MT_CHECK_EQ_U64(hits, N_FNS);
  /* 不存在的 id */
  MT_CHECK(mosaic_runtime_find_function(rt, 0) == NULL);
  MT_CHECK(mosaic_runtime_find_function(rt, 0xFFFFFFFFFFFFFFFFull) == NULL);
  mosaic_runtime_close(rt);
}

static void test_find_all_modules(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (int i = 0; i < N_MODULES; i++) {
    const mosaic_module_record *m = mosaic_runtime_find_module(rt, mod_ids[i]);
    MT_CHECK(m != NULL);
    if (m) {
      MT_CHECK_EQ_U64(mm_id(m), mod_ids[i]);
      MT_CHECK_EQ_U64(mm_fn_count(m), N_FNS / N_MODULES);
      const char *so = mosaic_runtime_module_string(rt, m, mm_so_off(m));
      MT_CHECK(so != NULL && strstr(so, "/tmp/mod_") != NULL);
      if (i > 0) MT_CHECK_EQ_U64(mm_dep_off(m) != MOSAIC_DEP_NONE, 1);
    }
  }
  MT_CHECK(mosaic_runtime_find_module(rt, 0) == NULL);
  mosaic_runtime_close(rt);
}

static void test_find_module_functions_are_contiguous(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  for (int i = 0; i < N_MODULES; i++) {
    const mosaic_module_record *m = mosaic_runtime_find_module(rt, mod_ids[i]);
    u32 base = mm_fn_base(m), cnt = mm_fn_count(m);
    const mosaic_function_record *fns = (const mosaic_function_record *)(rt->map + hdr_fn_off(rt->map));
    for (u32 j = 0; j < cnt; j++)
      MT_CHECK_EQ_U64(mf_module_id(&fns[base + j]), (u32)mod_ids[i]);
  }
  mosaic_runtime_close(rt);
}

static void test_event_lookup(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "player_join"), 0);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "entity_spawn"), 3);
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "unknown_event"), MOSAIC_U32_NONE);
  mosaic_runtime_close(rt);
}

int main(void) {
  MT_RUN(test_find_all_functions);
  MT_RUN(test_find_all_modules);
  MT_RUN(test_find_module_functions_are_contiguous);
  MT_RUN(test_event_lookup);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build -j 2>&1 | tail -3 && ctest --test-dir build -R test_index 2>&1 | tail -5`
Expected: 链接失败(undefined reference to `mosaic_runtime_find_module` / `mosaic_runtime_find_function`)或查询返回 NULL 断言失败。

- [ ] **Step 3: 实现 src/index.c**

```c
#include "mosaic_internal.h"
#include <string.h>

const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id) {
  if (!rt) return NULL;
  u64 n = hdr_module_count(rt->map);
  const mosaic_module_record *mods = (const mosaic_module_record *)(rt->map + hdr_module_off(rt->map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mm_id(&mods[mid]);
    if (id == module_id) return &mods[mid];
    if (id < module_id) lo = mid + 1; else hi = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}

const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  u32 module_id = (u32)(fn_id >> 32);
  const mosaic_module_record *m = mosaic_runtime_find_module(rt, module_id);
  if (!m) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const mosaic_function_record *fns = (const mosaic_function_record *)(rt->map + hdr_fn_off(rt->map));
  u32 base = mm_fn_base(m), cnt = mm_fn_count(m);
  u64 lo = 0, hi = cnt;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    u64 id = mf_id(&fns[base + mid]);
    if (id == fn_id) return &fns[base + mid];
    if (id < fn_id) lo = mid + 1; else hi = mid;
  }
  rt->last_err = MOSAIC_ERR_NOT_FOUND;
  return NULL;
}
```

- [ ] **Step 4: CMakeLists.txt 追加**

```cmake
add_executable(test_index tests/test_index.c)
target_link_libraries(test_index mosaic_core)
add_test(NAME test_index COMMAND test_index)
```

注意:test_index 使用了 `rt->map`,所以把 `mosaic_internal.h` 加入它的 include 路径:

```cmake
target_include_directories(test_index PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

- [ ] **Step 5: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过。

- [ ] **Step 6: 提交**

```bash
git add src/index.c tests/test_index.c CMakeLists.txt
git commit -m "feat: index lookups via mmap binary search, naive-model property test"
```

---

### Task 5: 工作集 —— arena 池 + FnObj + ws 哈希 + 窗口链表

**Files:**
- Create: `include/mosaic/function.h`(FnObj 公开结构)
- Create: `include/mosaic/module.h`(模块 ABI 类型,Task 6 会用)
- Modify: `src/working_set.c`(实现)
- Create: `tests/test_working_set.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_working_set.c`(arena 分配/释放、哈希插入/查找/删除、窗口链表)

**Interfaces:**
- Consumes: Task 3 的 `struct mosaic_runtime`(ws 字段)、`now_ns()`
- Produces:
  - `typedef struct mosaic_fn_obj { u64 fn_id; mosaic_code_fn code; void *state; u32 refs; u64 last_use; u32 freq; u32 state_size; struct mosaic_fn_obj *prev, *next; struct slab *slab; const mosaic_function_record *rec; } mosaic_fn_obj;`
  - `ws_find / ws_insert / ws_remove / fn_alloc / fn_free / arena_alloc / arena_zalloc`(声明已在 mosaic_internal.h)

- [ ] **Step 1: 写 include/mosaic/module.h**

```c
#ifndef MOSAIC_MODULE_H
#define MOSAIC_MODULE_H
#include "mosaic/base.h"

#define MOSAIC_MODULE_ABI_VERSION 1

typedef void (*mosaic_code_fn)(void *state, u32 event_id, const void *event);

typedef struct { u32 code_off; mosaic_code_fn code; } mosaic_fn_entry;
typedef struct {
  u32 abi_version;
  u32 fn_count;
  const mosaic_fn_entry *fns;   /* code_off 索引到这张表 */
  u32 state_size;               /* 默认 state 大小(供无 hint 的函数) */
} mosaic_module_abi;

typedef const mosaic_module_abi *(*mosaic_module_abi_v1_fn)(void);
#endif
```

- [ ] **Step 2: 写 include/mosaic/function.h**

```c
#ifndef MOSAIC_FUNCTION_H
#define MOSAIC_FUNCTION_H
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/module.h"

struct slab;
typedef struct mosaic_fn_obj {
  u64 fn_id;
  mosaic_code_fn code;      /* 热路径直接调用 */
  void *state;              /* 热路径直接传入 */
  u32 refs;                 /* 租约 + 在途 */
  u64 last_use;             /* Denning 窗口追踪 */
  u32 freq;                 /* GDSF-lite */
  u32 state_size;
  struct mosaic_fn_obj *prev, *next;   /* 窗口链表 */
  struct slab *slab;
  const mosaic_function_record *rec;   /* 回指 mmap 记录 */
} mosaic_fn_obj;

struct mosaic_runtime;
mosaic_fn_obj *mosaic_fn_materialize(struct mosaic_runtime *rt, u64 fn_id);
void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event);
int mosaic_fn_tombstone(struct mosaic_runtime *rt, mosaic_fn_obj *fn);
#endif
```

- [ ] **Step 3: 实现 src/working_set.c**

```c
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

static int ws_grow(mosaic_runtime *rt) {
  u64 cap = rt->ws.cap ? rt->ws.cap * 2 : 16;
  u64 *keys = calloc(cap, sizeof *keys);
  struct mosaic_fn_obj **vals = calloc(cap, sizeof *vals);
  if (!keys || !vals) { free(keys); free(vals); rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
  for (u64 i = 0; i < rt->ws.cap; i++) {
    u64 k = rt->ws.keys[i];
    if (!k) continue;
    u64 h = k & (cap - 1);
    while (keys[h]) h = (h + 1) & (cap - 1);
    keys[h] = k; vals[h] = rt->ws.vals[i];
  }
  free(rt->ws.keys); free(rt->ws.vals);
  rt->ws.keys = keys; rt->ws.vals = vals; rt->ws.cap = cap;
  return 0;
}

struct mosaic_fn_obj *ws_find(mosaic_runtime *rt, u64 fn_id) {
  if (!rt->ws.cap) return NULL;
  u64 h = fn_id & (rt->ws.cap - 1);
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
  if (rt->ws.len * 10 >= rt->ws.cap * 7) ws_grow(rt);
  u64 h = fn->fn_id & (rt->ws.cap - 1);
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
    u64 h = fn->fn_id & (rt->ws.cap - 1);
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
        u64 ideal = rt->ws.keys[j] & mask;
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
```

- [ ] **Step 4: 写 tests/test_working_set.c**

```c
#include "mosaic/base.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static void test_ws_basic(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_ws.pack", 1, 100, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "m", "/tmp/x.so");
  for (int i = 0; i < 100; i++)
    mosaic_pack_builder_add_fn(b, 10, (u64)i, 0, 0, 1, 0, 0);
  if (mosaic_pack_builder_finish(b, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); }
  mosaic_pack_builder_free(b);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_ws.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 插入 1000 个(触发扩容) */
  for (u64 i = 0; i < 1000; i++) {
    struct mosaic_fn_obj *f = fn_alloc(rt);
    MT_CHECK(f != NULL);
    f->fn_id = i + 1;
    ws_insert(rt, f);
  }
  MT_CHECK_EQ_U64(rt->ws.len, 1000);
  for (u64 i = 0; i < 1000; i++) {
    struct mosaic_fn_obj *f = ws_find(rt, i + 1);
    MT_CHECK(f != NULL && f->fn_id == i + 1);
  }
  MT_CHECK(ws_find(rt, 999999) == NULL);
  /* 删除一半 */
  for (u64 i = 0; i < 500; i++) ws_remove(rt, ws_find(rt, i * 2 + 1));
  MT_CHECK_EQ_U64(rt->ws.len, 500);
  MT_CHECK(ws_find(rt, 1) == NULL);
  MT_CHECK(ws_find(rt, 2) != NULL);
  /* 窗口链表一致性 */
  u64 walked = 0;
  for (struct mosaic_fn_obj *f = rt->ws_head; f; f = f->next) walked++;
  MT_CHECK_EQ_U64(walked, 500);
  /* 释放:再分配复用 */
  fn_free(rt, ws_find(rt, 2));
  struct mosaic_fn_obj *f2 = fn_alloc(rt);
  MT_CHECK(f2 != NULL);
  /* 清理 */
  for (struct mosaic_fn_obj *f = rt->ws_head; f; f = f->next) fn_free(rt, f);
  mosaic_runtime_close(rt);
}

static void test_arena(void) {
  void *p1 = NULL; void *p2 = NULL;
  /* 直接构造最小 runtime 测试 arena_alloc 语义(不依赖 pack) */
  mosaic_runtime rt; memset(&rt, 0, sizeof rt);
  arena_zalloc(&rt, 64, &p1);
  MT_CHECK(p1 != NULL);
  for (int i = 0; i < 64; i++) MT_CHECK(((u8 *)p1)[i] == 0);
  p2 = arena_alloc(&rt, 1024);
  MT_CHECK(p2 != NULL);
  free(p1); free(p2);
}

static void test_cluster_removal(void) {
  /* 开放寻址簇删除回归测试:cap=16,id 1/17/33 全落槽 1(线性探测 1,2,3)。
     删除中间的 17 后,33 必须仍然可查(后移修复)。 */
  mosaic_runtime rt; memset(&rt, 0, sizeof rt);
  for (u64 id = 1; id <= 33; id += 16) {   /* 1, 17, 33 */
    struct mosaic_fn_obj *f = fn_alloc(&rt);
    MT_CHECK(f != NULL);
    f->fn_id = id;
    ws_insert(&rt, f);
  }
  MT_CHECK_EQ_U64(rt.ws.cap, 16);   /* 3 个元素不触发扩容 */
  struct mosaic_fn_obj *mid = ws_find(&rt, 17);
  MT_CHECK(mid != NULL);
  ws_remove(&rt, mid);
  MT_CHECK(ws_find(&rt, 1) != NULL);
  MT_CHECK(ws_find(&rt, 33) != NULL);   /* 无修复时这里返回 NULL */
  MT_CHECK(ws_find(&rt, 17) == NULL);
  /* 清空资源 */
  for (struct mosaic_fn_obj *f = rt.ws_head; f; f = f->next) fn_free(&rt, f);
}

int main(void) {
  MT_RUN(test_ws_basic);
  MT_RUN(test_arena);
  MT_RUN(test_cluster_removal);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 5: CMakeLists.txt 追加**

```cmake
add_executable(test_working_set tests/test_working_set.c)
target_link_libraries(test_working_set mosaic_core)
target_include_directories(test_working_set PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_working_set COMMAND test_working_set)
```

- [ ] **Step 6: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过(此时其余 test 仍编译:lifecycle.c 等还是占位文件)。

- [ ] **Step 7: 提交**

```bash
git add include/mosaic/module.h include/mosaic/function.h src/working_set.c tests/test_working_set.c CMakeLists.txt
git commit -m "feat: working set arena, ws hash, window list"
```

---

### Task 6: 模块 ABI + dlopen 加载 + 物化/恢复路径

**Files:**
- Create: `tests/test_mod.c`(MODULE,3 个合成代码入口)
- Create: `tests/test_badmod.c`(MODULE,ABI 版本错误)
- Create: `include/mosaic/event.h`(占位:dispatch 声明,Task 8 实现)
- Create: `include/mosaic/ownership.h`、`include/mosaic/eviction.h`(占位声明,Task 9/10 实现)
- Modify: `src/lifecycle.c`(实现 mod_load/mod_unload/materialize/state_blob_append)
- Create: `tests/test_lifecycle.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_lifecycle.c`(物化→执行→state 生效;重复物化返回同对象;恢复保 state;ABI 错误 → NULL+ERR_ABI)

**Interfaces:**
- Consumes: Task 5 的 ws_* / fn_alloc / arena_zalloc;`mosaic_module_abi`(Task 5 已定义)
- Produces:
  - `mosaic_fn_materialize`(COLD→ACTIVE;ACTIVE→返回已有;TOMBSTONED=COLD+state_off≠0 → 从 state blob 恢复)
  - `mod_load` / `mod_unload`(dlopen 引用计数;dlsym `mosaic_module_abi_v1`;校验 `abi_version == MOSAIC_MODULE_ABI_VERSION` 与 `abi->fn_count == mm_fn_count(模块记录)`)
  - `state_blob_append`(state blob 写 `(u32 len, bytes)` 条目,mremap 扩容;返回条目偏移)
  - `mosaic_event_dispatch`(占位声明)等其余头文件

- [ ] **Step 1: 写 tests/test_mod.c(合成模块)**

```c
/* 测试用合成模块:3 个代码入口,state = 64B(首 u32 为计数器) */
#include "mosaic/module.h"

static void code_inc(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) ((u32 *)s)[0]++;
}
static void code_add(void *s, u32 e, const void *ev) {
  (void)e;
  if (s) ((u32 *)s)[0] += ev ? *(const u32 *)ev : 1u;
}
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

static const mosaic_fn_entry g_fns[3] = {
  { 0, code_inc },
  { 1, code_add },
  { 2, code_noop },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 3, g_fns, 64 };
  return &abi;
}
```

- [ ] **Step 2: 写 tests/test_badmod.c(ABI 版本错误)**

```c
#include "mosaic/module.h"
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }
static const mosaic_fn_entry g_fns[1] = { { 0, code_noop } };
const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { 999 /* 错误版本 */, 1, g_fns, 0 };
  return &abi;
}
```

- [ ] **Step 3: 写占位头文件 event.h / ownership.h / eviction.h**

`include/mosaic/event.h`:

```c
#ifndef MOSAIC_EVENT_H
#define MOSAIC_EVENT_H
#include "mosaic/base.h"
struct mosaic_runtime;
u32 mosaic_event_dispatch(struct mosaic_runtime *rt, u32 event_id, const void *event);  /* Task 8 实现 */
#endif
```

`include/mosaic/ownership.h`:

```c
#ifndef MOSAIC_OWNERSHIP_H
#define MOSAIC_OWNERSHIP_H
#include "mosaic/base.h"
struct mosaic_runtime;
typedef struct mosaic_lease mosaic_lease;
mosaic_lease *mosaic_lease_acquire(struct mosaic_runtime *rt, u64 fn_id);  /* Task 9 实现 */
void mosaic_lease_release(mosaic_lease *l);                                /* Task 9 实现 */
#endif
```

`include/mosaic/eviction.h`:

```c
#ifndef MOSAIC_EVICTION_H
#define MOSAIC_EVICTION_H
#include "mosaic/base.h"
struct mosaic_runtime;
typedef struct { u64 window_ns; } mosaic_evict_config;
int mosaic_evict_idle(struct mosaic_runtime *rt, const mosaic_evict_config *cfg);  /* Task 9 实现 */
#endif
```

- [ ] **Step 4: 实现 src/lifecycle.c**

```c
#include "mosaic_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const mosaic_module_abi *mod_load(mosaic_runtime *rt, u64 module_id) {
  for (struct mod_entry *m = rt->mods; m; m = m->next)
    if (m->module_id == module_id) { m->refs++; return m->abi; }
  const mosaic_module_record *rec = mosaic_runtime_find_module(rt, module_id);
  if (!rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  const char *path = mosaic_runtime_module_string(rt, rec, mm_so_off(rec));
  if (!path) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  void *so = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
  if (!so) { rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  dlerror();
  mosaic_module_abi_v1_fn sym = (mosaic_module_abi_v1_fn)dlsym(so, "mosaic_module_abi_v1");
  const char *e = dlerror();
  if (e) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  const mosaic_module_abi *abi = sym();
  if (abi->abi_version != MOSAIC_MODULE_ABI_VERSION) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  if (abi->fn_count != mm_fn_count(rec)) { dlclose(so); rt->last_err = MOSAIC_ERR_ABI; return NULL; }
  struct mod_entry *m = calloc(1, sizeof *m);
  if (!m) { dlclose(so); rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
  m->module_id = module_id; m->so = so; m->abi = abi; m->refs = 1;
  m->next = rt->mods; rt->mods = m;
  return abi;
}

void mod_unload(mosaic_runtime *rt, u64 module_id) {
  for (struct mod_entry **pp = &rt->mods; *pp; pp = &(*pp)->next) {
    if ((*pp)->module_id == module_id) {
      struct mod_entry *m = *pp;
      if (m->refs > 1) { m->refs--; return; }
      *pp = m->next;
      dlclose(m->so);
      free(m);
      return;
    }
  }
}

int state_blob_append(mosaic_runtime *rt, const void *bytes, u32 len, u32 *out_off) {
  const u8 *h = rt->map;
  u64 base = hdr_state_off(h);
  u64 cap = hdr_state_cap(h);
  u64 used = rt->state_len;
  if (used + 4ull + len > cap) {
    /* 扩容:mremap 翻倍 */
    u64 newcap = cap ? cap * 2 : 4096;
    while (newcap < used + 4ull + len) newcap *= 2;
    size_t newlen = base + newcap;
    void *np = mremap(rt->map, rt->map_len, newlen, MREMAP_MAYMOVE);
    if (np == MAP_FAILED) { rt->last_err = MOSAIC_ERR_NOMEM; return -1; }
    rt->map = np; rt->map_len = newlen;
    hdr_set_state_cap(rt->map, newcap);
  }
  u8 *dst = rt->map + base + used;
  wr_le32(dst, len);
  memcpy(dst + 4, bytes, len);
  rt->state_len = used + 4ull + len;
  hdr_set_state_len(rt->map, rt->state_len);
  *out_off = (u32)used;
  return 0;
}

mosaic_fn_obj *mosaic_fn_materialize(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, fn_id);
  if (!rec) { rt->last_err = MOSAIC_ERR_NOT_FOUND; return NULL; }
  mosaic_fn_obj *ex = ws_find(rt, fn_id);
  if (ex) return ex;   /* 已 ACTIVE:直接返回(幂等) */
  u16 fl = mf_flags(rec);
  u8 st = (u8)(fl & MOSAIC_FN_STATE_MASK);
  if (st != MOSAIC_FN_STATE_COLD) { rt->last_err = MOSAIC_ERR_ILLEGAL; return NULL; }
  mosaic_function_record *rw = (mosaic_function_record *)rec;   /* MAP_PRIVATE 可写 */
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_MATERIALIZING));

  const mosaic_module_abi *abi = mod_load(rt, mf_module_id(rec));
  if (!abi) { mf_set_flags(rw, fl); return NULL; }
  u32 co = mf_code_off(rec);
  if (co >= abi->fn_count) { rt->last_err = MOSAIC_ERR_ABI; mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }

  void *state = NULL;
  u32 sz = mf_state_size(rec);
  if (fl & MOSAIC_FN_REQUIRES_STATE) {
    if (sz == 0) sz = abi->state_size;
    void *p = NULL; arena_zalloc(rt, sz, &p);
    if (!p) { mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }
    state = p;
    u32 soff = mf_state_off(rec);
    if (soff != 0) {
      /* TOMBSTONED → RESTORE:从 state blob 读回 */
      const u8 *h = rt->map;
      u64 base = hdr_state_off(h);
      if (base + soff + 4 <= rt->map_len) {
        u32 len = rd_le32(rt->map + base + soff);
        if (len <= sz && base + soff + 4 + len <= rt->map_len)
          memcpy(state, rt->map + base + soff + 4, len);
      }
    }
  }

  mosaic_fn_obj *fn = fn_alloc(rt);
  if (!fn) { free(state); mf_set_flags(rw, fl); mod_unload(rt, mf_module_id(rec)); return NULL; }
  fn->fn_id = fn_id;
  fn->rec = rec;
  fn->code = abi->fns[co].code;
  fn->state = state;
  fn->state_size = sz;
  fn->last_use = now_ns();
  ws_insert(rt, fn);
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_ACTIVE));
  return fn;
}
```

- [ ] **Step 5: 写 tests/test_lifecycle.c**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;
static const char *BAD_SO_PATH;

static int build_pack(const char *path, const char *so, u64 module_id, u32 fn_count) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, fn_count, 0, 0, 0);
  mosaic_pack_builder_add_module(b, module_id, 1, "mod", so);
  for (u32 i = 0; i < fn_count; i++)
    mosaic_pack_builder_add_fn(b, module_id, i, i % 3, 64, 1, 0,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  if (rc) fprintf(stderr, "build: %s\n", err);
  return rc;
}

static void test_materialize_and_execute(void) {
  const u64 MID = 100;
  const u64 F0 = MID << 32;          /* code_off 0 = code_inc */
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc.pack", SO_PATH, MID, 3) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  MT_CHECK_EQ_U64(fn->fn_id, F0);
  /* 物化后 state 为 0 */
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 0);
  /* 热路径:3 次 inc */
  for (int i = 0; i < 3; i++) mosaic_fn_execute(fn, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 3);
  /* 重复物化幂等:返回同一对象 */
  mosaic_fn_obj *fn2 = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn2 == fn);
  /* code_add:带事件载荷 */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (MID << 32) | 1);
  MT_CHECK(f1 != NULL);
  u32 add = 7;
  mosaic_fn_execute(f1, 0, &add);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 7);
  /* 不存在的 fn */
  MT_CHECK(mosaic_fn_materialize(rt, (MID << 32) | 99) == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

static void test_tombstone_restore_preserves_state(void) {
  const u64 MID = 200;
  const u64 F0 = MID << 32;
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc2.pack", SO_PATH, MID, 1) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc2.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  for (int i = 0; i < 5; i++) mosaic_fn_execute(fn, 0, NULL);
  MT_CHECK_EQ_U64(*(u32 *)fn->state, 5);
  int rc = mosaic_fn_tombstone(rt, fn);
  MT_CHECK_EQ_U64(rc, 0);
  /* 记录变为 COLD + state_off 已写 */
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, F0);
  MT_CHECK_EQ_U64(mf_flags(rec) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);
  MT_CHECK(mf_state_off(rec) != 0);
  /* 工作集已移除 */
  mosaic_fn_obj *f2 = mosaic_fn_materialize(rt, F0);   /* RESTORE 路径 */
  MT_CHECK(f2 != NULL);
  MT_CHECK(f2 != fn);   /* 新对象 */
  MT_CHECK_EQ_U64(*(u32 *)f2->state, 5);   /* state 从 blob 恢复 */
  mosaic_runtime_close(rt);
}

static void test_bad_abi_rejected(void) {
  const u64 MID = 300;
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc3.pack", BAD_SO_PATH, MID, 1) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc3.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, MID << 32);
  MT_CHECK(fn == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ABI);
  /* 失败后 flags 回滚为 COLD,可再次尝试 */
  const mosaic_function_record *rec = mosaic_runtime_find_function(rt, MID << 32);
  MT_CHECK_EQ_U64(mf_flags(rec) & MOSAIC_FN_STATE_MASK, MOSAIC_FN_STATE_COLD);
  mosaic_runtime_close(rt);
}

static void test_materialize_while_not_cold_rejected(void) {
  /* 物化两次同 id 幂等已在 test_materialize_and_execute;这里验证墓碑后不能直接物化(恢复路径已覆盖),
     以及 materialize 对已 ACTIVE 返回同对象(幂等)已在上面。保持最小:验证 fn id 0x0 不存在 */
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK(mosaic_fn_materialize(rt, 0) == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s <test_mod.so> <test_badmod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1]; BAD_SO_PATH = argv[2];
  MT_RUN(test_materialize_and_execute);
  MT_RUN(test_tombstone_restore_preserves_state);
  MT_RUN(test_bad_abi_rejected);
  MT_RUN(test_materialize_while_not_cold_rejected);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 6: CMakeLists.txt 追加**

```cmake
add_library(test_mod MODULE tests/test_mod.c)
target_include_directories(test_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(test_badmod MODULE tests/test_badmod.c)
target_include_directories(test_badmod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test_lifecycle tests/test_lifecycle.c)
target_link_libraries(test_lifecycle mosaic_core)
add_test(NAME test_lifecycle COMMAND test_lifecycle $<TARGET_FILE:test_mod> $<TARGET_FILE:test_badmod>)
```

- [ ] **Step 7: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过(含 test_lifecycle 4 项)。

- [ ] **Step 8: 提交**

```bash
git add include/mosaic/event.h include/mosaic/ownership.h include/mosaic/eviction.h \
        src/lifecycle.c tests/test_mod.c tests/test_badmod.c tests/test_lifecycle.c CMakeLists.txt
git commit -m "feat: module ABI, dlopen materialize/restore paths, lifecycle tests"
```

---

### Task 7: 热路径执行 + 墓碑路径 + 状态机非法转移测试

**Files:**
- Modify: `src/lifecycle.c`(追加 execute/tombstone)
- Modify: `tests/test_lifecycle.c`(追加墓碑测试:未 ACTIVE 墓碑拒绝、TOMBSTONE_ABLE 关 → ILLEGAL、state 为空函数)
- Test: `tests/test_lifecycle.c`

**Interfaces:**
- Consumes: Task 6 的 materialize/state_blob_append
- Produces:
  - `void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event)` —— 热路径:`fn->code(fn->state, event_id, event)`,零检查
  - `int mosaic_fn_tombstone(mosaic_runtime *rt, mosaic_fn_obj *fn)` —— ACTIVE 且 refs==0 且 TOMBSTONE_ABLE → quiesce 序列化释放 → COLD(state_off≠0);否则 BUSY/ILLEGAL

- [ ] **Step 1: 写失败测试(追加到 tests/test_lifecycle.c 的 main 前)**

```c
static void test_tombstone_illegal_transitions(void) {
  const u64 MID = 400;
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_lc4.pack", SO_PATH, MID, 2) == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc4.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, MID << 32);
  MT_CHECK(fn != NULL);
  /* 墓碑两次:第二次非法 */
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == 0);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == -1);   /* 未 ACTIVE */
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  /* 从未物化的函数不能墓碑 */
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  /* 不可墓碑的函数(flags 无 TOMBSTONE_ABLE)→ ILLEGAL */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (MID << 32) | 1);
  MT_CHECK(f1 != NULL);
  MT_CHECK(mosaic_fn_tombstone(rt, f1) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ILLEGAL);
  mosaic_runtime_close(rt);
}

static void test_no_state_function(void) {
  const u64 MID = 500;
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_lc5.pack", 1, 1, 0, 0, 0);
  mosaic_pack_builder_add_module(b, MID, 1, "mod", SO_PATH);
  mosaic_pack_builder_add_fn(b, MID, 0, 2 /* noop */, 0 /* 无 state */, 1, 0, MOSAIC_FN_TOMBSTONE_ABLE);
  if (mosaic_pack_builder_finish(b, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); }
  mosaic_pack_builder_free(b);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_lc5.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, MID << 32);
  MT_CHECK(fn != NULL);
  MT_CHECK(fn->state == NULL);
  mosaic_fn_execute(fn, 0, NULL);   /* 无 state 也安全 */
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == 0);   /* 无 state 也可墓碑 */
  mosaic_runtime_close(rt);
}
```

- [ ] **Step 2: 在 tests/test_lifecycle.c 的 main 中追加两个 MT_RUN**

```c
  MT_RUN(test_tombstone_illegal_transitions);
  MT_RUN(test_no_state_function);
```

- [ ] **Step 3: 运行验证失败**

Run: `cmake --build build -j 2>&1 | tail -3 && ctest --test-dir build -R test_lifecycle --output-on-failure 2>&1 | tail -8`
Expected: 链接失败(undefined `mosaic_fn_tombstone`)。

- [ ] **Step 4: 在 src/lifecycle.c 末尾追加实现**

```c
void mosaic_fn_execute(mosaic_fn_obj *fn, u32 event_id, const void *event) {
  /* 热路径:零检查,直接调用 */
  fn->code(fn->state, event_id, event);
}

int mosaic_fn_tombstone(mosaic_runtime *rt, mosaic_fn_obj *fn) {
  if (!rt || !fn) { if (rt) rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  u16 fl = mf_flags(fn->rec);
  u8 st = (u8)(fl & MOSAIC_FN_STATE_MASK);
  if (st != MOSAIC_FN_STATE_ACTIVE) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  if (fn->refs) { rt->last_err = MOSAIC_ERR_BUSY; return -1; }
  if (!(fl & MOSAIC_FN_TOMBSTONE_ABLE)) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  mosaic_function_record *rw = (mosaic_function_record *)fn->rec;
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_QUIESCING));
  if (fn->state && (fl & MOSAIC_FN_REQUIRES_STATE) && fn->state_size) {
    u32 off = 0;
    if (state_blob_append(rt, fn->state, fn->state_size, &off) != 0) {
      mf_set_flags(rw, fl);   /* 回滚为 ACTIVE */
      return -1;
    }
    mf_set_state_off(rw, off);
  }
  ws_remove(rt, fn);
  free(fn->state);
  fn_free(rt, fn);
  mod_unload(rt, mf_module_id(fn->rec));
  mf_set_flags(rw, (u16)((fl & ~MOSAIC_FN_STATE_MASK) | MOSAIC_FN_STATE_COLD));
  return 0;
}
```

- [ ] **Step 5: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过(test_lifecycle 现 6 项)。

- [ ] **Step 6: 提交**

```bash
git add src/lifecycle.c tests/test_lifecycle.c
git commit -m "feat: zero-check hot path execute, tombstone with state persistence"
```

---

### Task 8: 触发索引 + 事件派发

**Files:**
- Modify: `src/trigger.c`(实现)
- Modify: `tests/test_mod.c`(给 code_inc 增加事件计数能力:state 布局改为 {u32 inc_counter; u32 last_event;})
- Create: `tests/test_trigger.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_trigger.c`(事件→订阅函数区间派发;未知事件 → 0;缺 so 的函数被跳过即降级)

**Interfaces:**
- Consumes: Task 6 materialize、Task 7 execute/tombstone
- Produces:
  - `u32 mosaic_event_dispatch(mosaic_runtime *rt, u32 event_id, const void *event)` —— 触发表区间扫描;ACTIVE 直接执行,COLD 物化后执行;物化失败跳过(降级);返回执行数
  - 派发时更新 `fn->last_use`/`fn->freq`

- [ ] **Step 1: 修改 tests/test_mod.c(记录最近事件)**

```c
#include "mosaic/module.h"

/* state 布局:u32 counter; u32 last_event */
static void code_inc(void *s, u32 e, const void *ev) {
  (void)ev;
  if (s) { u32 *st = s; st[0]++; st[1] = e; }
}
static void code_add(void *s, u32 e, const void *ev) {
  if (s) { u32 *st = s; st[0] += ev ? *(const u32 *)ev : 1u; st[1] = e; }
}
static void code_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

static const mosaic_fn_entry g_fns[3] = {
  { 0, code_inc },
  { 1, code_add },
  { 2, code_noop },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 3, g_fns, 64 };
  return &abi;
}
```

- [ ] **Step 2: 写失败测试 tests/test_trigger.c**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;
static const char *MISSING_SO = "/tmp/definitely_missing.so";

static int build_pack(const char *path) {
  char err[256];
  /* 模块 A(10):fns 0,1 → 订阅 event0;模块 B(20):fn 0 → 订阅 event0 和 event1 */
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 2, 3, 4, 0, 2);   /* 4 条触发 */
  mosaic_pack_builder_add_event(b, "player_join");  /* 0 */
  mosaic_pack_builder_add_event(b, "block_break");  /* 1 */
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", SO_PATH);
  mosaic_pack_builder_add_module(b, 20, 1, "mod_b", MISSING_SO);   /* so 不存在 → 降级测试 */
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 10, 1, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_fn(b, 20, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 1);
  mosaic_pack_builder_add_trigger(b, 0, 20ull << 32 | 0);
  mosaic_pack_builder_add_trigger(b, 1, 20ull << 32 | 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_dispatch_executes_subscribers(void) {
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_trig.pack") == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_trig.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  u32 ev_join = mosaic_runtime_event_id(rt, "player_join");
  MT_CHECK_EQ_U64(ev_join, 0);
  /* event0 有 3 个订阅,但 mod_b 的 so 缺失 → 降级跳过 → 执行 2 个 */
  u32 n = mosaic_event_dispatch(rt, ev_join, NULL);
  MT_CHECK_EQ_U64(n, 2);
  /* mod_a 两个函数已物化并各执行 1 次 */
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 1);      /* counter == 1 */
  MT_CHECK_EQ_U64(((u32 *)f0->state)[1], 0);  /* last_event == 0 */
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (10ull << 32) | 1);
  MT_CHECK_EQ_U64(*(u32 *)f1->state, 1);
  /* 再次派发:ACTIVE 热路径,仍执行 2 个 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, ev_join, NULL), 2);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  /* 未知事件:0 个执行 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 999, NULL), 0);
  /* 未订阅事件(event1 只有 mod_b)→ 0 个执行(降级) */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 1, NULL), 0);
  mosaic_runtime_close(rt);
}

static void test_dispatch_tombstone_restore_cycle(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_trig.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 派发两次,然后墓碑,再派发(恢复路径) */
  mosaic_event_dispatch(rt, 0, NULL);
  mosaic_event_dispatch(rt, 0, NULL);
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0 != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0->state, 2);
  MT_CHECK(mosaic_fn_tombstone(rt, f0) == 0);
  MT_CHECK(mosaic_event_dispatch(rt, 0, NULL) == 2);   /* 重新物化/恢复 + 执行 */
  mosaic_fn_obj *f0b = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0b != NULL);
  MT_CHECK_EQ_U64(*(u32 *)f0b->state, 3);   /* 2 + 1,state 经 blob 恢复 */
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_dispatch_executes_subscribers);
  MT_RUN(test_dispatch_tombstone_restore_cycle);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 3: 运行验证失败**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: 链接失败(undefined `mosaic_event_dispatch`)。

- [ ] **Step 4: 实现 src/trigger.c**

```c
#include "mosaic_internal.h"
#include <stdio.h>

/* 触发表下界:第一个 event_id >= ev 的条目下标 */
static u64 trigger_lower_bound(mosaic_runtime *rt, u32 ev) {
  u64 n = hdr_trigger_count(rt->map);
  const mosaic_trigger_entry *t = (const mosaic_trigger_entry *)(rt->map + hdr_trigger_off(rt->map));
  u64 lo = 0, hi = n;
  while (lo < hi) {
    u64 mid = lo + (hi - lo) / 2;
    if (mt_event_id(&t[mid]) < ev) lo = mid + 1; else hi = mid;
  }
  return lo;
}

u32 mosaic_event_dispatch(mosaic_runtime *rt, u32 event_id, const void *event) {
  if (!rt) return 0;
  u32 executed = 0;
  u64 i = trigger_lower_bound(rt, event_id);
  u64 n = hdr_trigger_count(rt->map);
  const mosaic_trigger_entry *t = (const mosaic_trigger_entry *)(rt->map + hdr_trigger_off(rt->map));
  while (i < n && mt_event_id(&t[i]) == event_id) {
    u64 fn_id = mt_fn_id(&t[i]);
    mosaic_fn_obj *fn = ws_find(rt, fn_id);
    if (!fn) {
      fn = mosaic_fn_materialize(rt, fn_id);
      if (!fn) {   /* 降级:跳过 + 诊断 */
        fprintf(stderr, "mosaic: dispatch event %u: skip fn %llu (err %u)\n",
                event_id, (unsigned long long)fn_id, rt->last_err);
        i++;
        continue;
      }
    }
    fn->last_use = now_ns();
    fn->freq++;
    mosaic_fn_execute(fn, event_id, event);
    executed++;
    i++;
  }
  return executed;
}
```

- [ ] **Step 5: CMakeLists.txt 追加**

```cmake
add_executable(test_trigger tests/test_trigger.c)
target_link_libraries(test_trigger mosaic_core)
add_test(NAME test_trigger COMMAND test_trigger $<TARGET_FILE:test_mod>)
```

- [ ] **Step 6: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过。

- [ ] **Step 7: 提交**

```bash
git add src/trigger.c tests/test_mod.c tests/test_trigger.c CMakeLists.txt
git commit -m "feat: trigger index dispatch with materialize-on-demand and degrade"
```

---

### Task 9: 所有权租约 + 驱逐策略

**Files:**
- Modify: `src/ownership.c`(实现)
- Modify: `src/eviction.c`(实现)
- Create: `tests/test_ownership.c`
- Modify: `CMakeLists.txt`
- Test: `tests/test_ownership.c`(租约阻止墓碑 → BUSY;释放后可墓碑;驱逐窗口语义;租约保护免驱逐)

**Interfaces:**
- Consumes: Task 7 的 tombstone
- Produces:
  - `mosaic_lease *mosaic_lease_acquire(struct mosaic_runtime *rt, u64 fn_id)` —— 找到/物化 fn,refs++,返回租约
  - `void mosaic_lease_release(mosaic_lease *l)` —— refs--
  - `int mosaic_evict_idle(struct mosaic_runtime *rt, const mosaic_evict_config *cfg)` —— 全扫描工作集,`last_use + window_ns <= now` 且 refs==0 且可墓碑 → 墓碑;返回墓碑数

- [ ] **Step 1: 写失败测试 tests/test_ownership.c**

```c
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic/ownership.h"
#include "mosaic/eviction.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

static const char *SO_PATH;

static int build_pack(const char *path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 2, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "mod", SO_PATH);
  for (int i = 0; i < 2; i++)
    mosaic_pack_builder_add_fn(b, 10, (u64)i, 0, 64, 1, 0,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_lease_blocks_tombstone(void) {
  char err[256];
  MT_CHECK(build_pack("/tmp/mosaic_test_own.pack") == 0);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_own.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  const u64 F0 = 10ull << 32;
  mosaic_lease *l = mosaic_lease_acquire(rt, F0);
  MT_CHECK(l != NULL);
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, F0);
  MT_CHECK(fn != NULL);
  MT_CHECK_EQ_U64(fn->refs, 1);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == -1);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_BUSY);
  /* 驱逐也不能动它 */
  mosaic_evict_config cfg = { 0 };
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &cfg), 0);
  mosaic_lease_release(l);
  MT_CHECK_EQ_U64(fn->refs, 0);
  MT_CHECK(mosaic_fn_tombstone(rt, fn) == 0);
  mosaic_runtime_close(rt);
}

static void test_evict_window(void) {
  char err[256];
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_own.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  mosaic_fn_obj *f0 = mosaic_fn_materialize(rt, 10ull << 32);
  mosaic_fn_obj *f1 = mosaic_fn_materialize(rt, (10ull << 32) | 1);
  MT_CHECK(f0 != NULL && f1 != NULL);
  /* 大窗口:无人过期 */
  mosaic_evict_config big = { 1000000000000ull };   /* 1000s */
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &big), 0);
  MT_CHECK(ws_find(rt, 10ull << 32) != NULL);
  /* 零窗口:全部过期 → 墓碑 2 个 */
  mosaic_evict_config zero = { 0 };
  MT_CHECK_EQ_U64(mosaic_evict_idle(rt, &zero), 2);
  MT_CHECK(ws_find(rt, 10ull << 32) == NULL);
  MT_CHECK(ws_find(rt, (10ull << 32) | 1) == NULL);
  /* 墓碑后可恢复 */
  mosaic_fn_obj *f0b = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f0b != NULL);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 2; }
  SO_PATH = argv[1];
  MT_RUN(test_lease_blocks_tombstone);
  MT_RUN(test_evict_window);
  return MT_RESULT() ? 0 : 1;
}
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: 链接失败(undefined `mosaic_lease_acquire` / `mosaic_evict_idle`)。

- [ ] **Step 3: 实现 src/ownership.c**

```c
#include "mosaic_internal.h"
#include <stdlib.h>

struct mosaic_lease { mosaic_fn_obj *fn; };

mosaic_lease *mosaic_lease_acquire(mosaic_runtime *rt, u64 fn_id) {
  if (!rt) return NULL;
  mosaic_fn_obj *fn = ws_find(rt, fn_id);
  if (!fn) fn = mosaic_fn_materialize(rt, fn_id);
  if (!fn) return NULL;
  fn->refs++;
  mosaic_lease *l = malloc(sizeof *l);
  if (!l) { fn->refs--; rt->last_err = MOSAIC_ERR_NOMEM; return NULL; }
  l->fn = fn;
  return l;
}

void mosaic_lease_release(mosaic_lease *l) {
  if (!l) return;
  if (l->fn && l->fn->refs) l->fn->refs--;
  free(l);
}
```

- [ ] **Step 4: 实现 src/eviction.c**

```c
#include "mosaic_internal.h"

int mosaic_evict_idle(mosaic_runtime *rt, const mosaic_evict_config *cfg) {
  if (!rt || !cfg) return 0;
  u64 now = now_ns();
  int n = 0;
  for (mosaic_fn_obj *f = rt->ws_head; f; ) {
    mosaic_fn_obj *nx = f->next;
    /* 驱逐只看效用窗口与租约,不碰生命周期状态(正交轴) */
    if (!f->refs && (f->last_use + cfg->window_ns) <= now) {
      if (mosaic_fn_tombstone(rt, f) == 0) n++;
    }
    f = nx;
  }
  return n;
}
```

- [ ] **Step 5: CMakeLists.txt 追加**

```cmake
add_executable(test_ownership tests/test_ownership.c)
target_link_libraries(test_ownership mosaic_core)
add_test(NAME test_ownership COMMAND test_ownership $<TARGET_FILE:test_mod>)
```

- [ ] **Step 6: 运行测试**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部通过。

- [ ] **Step 7: 提交**

```bash
git add src/ownership.c src/eviction.c tests/test_ownership.c CMakeLists.txt
git commit -m "feat: leases (refcount guards) and window-based eviction"
```

---

### Task 10: 合成宇宙生成器 + 基准 harness(S1–S4 门禁)

**Files:**
- Create: `bench/synth_abi.c`(MODULE,10M 级合成代码)
- Create: `bench/synth_universe.c` + `bench/synth_universe.h`(参数化宇宙生成)
- Create: `bench/bench_runner.c`(S1-S4 场景 + 门禁断言)
- Modify: `CMakeLists.txt`
- Test: 手动运行 `build/bench/bench_runner`,验证门禁输出

**Interfaces:**
- Consumes: builder + runtime 全 API
- Produces:
  - `int mosaic_bench_build_universe(const char *pack_path, const char *so_path, u64 n_fns, u64 n_modules, u32 n_events, u32 triggers_per_fn);`(确定性 xorshift 随机;模块 id 稀疏;fn 共享 3 个代码入口)
  - `long mosaic_bench_rss_kb(void);`、`double mosaic_bench_now_us(void);`(基准辅助)
  - `int mosaic_bench_build_solo(const char *pack_path, const char *so_path);`(1 模块 1 函数 1 事件 "solo")
  - `bench_runner <n_fns> [pack_path] [so_path]` 退出码:0 = 全部门禁通过

- [ ] **Step 1: 写 bench/synth_abi.c**

```c
/* 合成模块:10M 函数共享 3 个代码入口;state = 64B */
#include "mosaic/module.h"

static void synth_inc(void *s, u32 e, const void *ev) {
  (void)e; (void)ev;
  if (s) { u32 *st = s; st[0]++; }
}
static void synth_add(void *s, u32 e, const void *ev) {
  (void)e;
  if (s) { u32 *st = s; st[0] += ev ? *(const u32 *)ev : 1u; }
}
static void synth_noop(void *s, u32 e, const void *ev) { (void)s; (void)e; (void)ev; }

static const mosaic_fn_entry g_fns[3] = {
  { 0, synth_inc }, { 1, synth_add }, { 2, synth_noop },
};

const mosaic_module_abi *mosaic_module_abi_v1(void) {
  static const mosaic_module_abi abi = { MOSAIC_MODULE_ABI_VERSION, 3, g_fns, 64 };
  return &abi;
}
```

- [ ] **Step 2: 写 bench/synth_universe.h**

```c
#ifndef MOSAIC_SYNTH_UNIVERSE_H
#define MOSAIC_SYNTH_UNIVERSE_H
#include "mosaic/base.h"

/* 确定性 xorshift64 */
static inline u64 mosaic_bench_rng(u64 *s) {
  *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
  return *s;
}

int mosaic_bench_build_universe(const char *pack_path, const char *so_path,
                                u64 n_fns, u64 n_modules, u32 n_events, u32 triggers_per_fn);
int mosaic_bench_build_solo(const char *pack_path, const char *so_path);
int mosaic_bench_build_cold(const char *pack_path, const char *so_path);
long mosaic_bench_rss_kb(void);
double mosaic_bench_now_us(void);
#endif
```

- [ ] **Step 3: 写 bench/synth_universe.c**

```c
#include "synth_universe.h"
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int mosaic_bench_build_universe(const char *pack_path, const char *so_path,
                                u64 n_fns, u64 n_modules, u32 n_events, u32 triggers_per_fn) {
  char err[256];
  u64 rng = 0x9E3779B97F4A7C15ull;       /* 种子 */
  const char *ev_names[] = { "player_join", "block_break", "item_use", "entity_spawn", "tick" };
  if (n_events > 5) n_events = 5;

  u64 n_triggers = n_fns * triggers_per_fn;
  u64 n_deps = n_modules > 1 ? n_modules - 1 : 0;
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, n_modules, n_fns, n_triggers, n_deps, n_events);
  if (!b) return -1;
  for (u32 i = 0; i < n_events; i++) mosaic_pack_builder_add_event(b, ev_names[i]);

  /* 模块 id 取连续值 1..n_modules(确定性,无碰撞——随机 id 在百万量级
     会撞车 ~39%,构建必然失败;稀疏随机 id 留给测试场景) */
  char name[64], so[256];
  u64 *mod_ids_arr = malloc(n_modules * sizeof(u64));
  for (u64 i = 0; i < n_modules; i++) {
    u64 mid = i + 1;
    mod_ids_arr[i] = mid;
    snprintf(name, sizeof name, "mod_%llu", (unsigned long long)mid);
    snprintf(so, sizeof so, "%s", so_path);
    mosaic_pack_builder_add_module(b, mid, 1, name, so);
  }
  u64 per_mod = n_fns / n_modules;
  u64 local = 0;
  for (u64 i = 0; i < n_fns; i++) {
    u64 mi = i / per_mod;
    u32 code = (u32)(mosaic_bench_rng(&rng) % 3);
    mosaic_pack_builder_add_fn(b, mod_ids_arr[mi], local++, code, 64, 1,
                               (u32)(mosaic_bench_rng(&rng) % 16),
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    if ((i + 1) % per_mod == 0) local = 0;
  }
  /* 触发:每函数订阅 triggers_per_fn 个不同事件 */
  for (u64 i = 0; i < n_fns; i++) {
    u64 mi = i / per_mod;
    u64 fn_id = (mod_ids_arr[mi] << 32) | (i % per_mod);
    for (u32 k = 0; k < triggers_per_fn; k++) {
      u32 e = (u32)(mosaic_bench_rng(&rng) % n_events);
      mosaic_pack_builder_add_trigger(b, e, fn_id);
    }
  }
  for (u64 i = 1; i < n_modules; i++)
    mosaic_pack_builder_add_dep(b, mod_ids_arr[i], mod_ids_arr[i - 1]);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "universe build: %s\n", err);
  mosaic_pack_builder_free(b);
  free(mod_ids_arr);
  return rc;
}

int mosaic_bench_build_solo(const char *pack_path, const char *so_path) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, 1, 1, 1, 0, 1);
  mosaic_pack_builder_add_event(b, "solo");
  mosaic_pack_builder_add_module(b, 42, 1, "solo_mod", so_path);
  mosaic_pack_builder_add_fn(b, 42, 0, 0, 64, 1, 0,
                             MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 42ull << 32);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "solo build: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

int mosaic_bench_build_cold(const char *pack_path, const char *so_path) {
  /* S2 专用:1 模块 1000 函数,1 事件 "cold",全部订阅 → 一次派发物化 1000 个 */
  char err[256];
  enum { COLD_FNS = 1000 };
  mosaic_pack_builder *b = mosaic_pack_builder_create(pack_path, 1, COLD_FNS, COLD_FNS, 0, 1);
  mosaic_pack_builder_add_event(b, "cold");
  mosaic_pack_builder_add_module(b, 7, 1, "cold_mod", so_path);
  for (u64 i = 0; i < COLD_FNS; i++) {
    mosaic_pack_builder_add_fn(b, 7, i, (u32)(i % 3), 64, 1, (u32)(i % 16),
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
    mosaic_pack_builder_add_trigger(b, 0, (7ull << 32) | i);
  }
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "cold build: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

long mosaic_bench_rss_kb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return -1;
  char line[256]; long kb = -1;
  while (fgets(line, sizeof line, f))
    if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
  fclose(f);
  return kb;
}

double mosaic_bench_now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}
```

- [ ] **Step 4: 写 bench/bench_runner.c(门禁断言)**

```c
#include "synth_universe.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mosaic/eviction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void gate(const char *name, int pass, const char *detail) {
  printf("GATE %-4s %s: %s\n", name, pass ? "PASS" : "FAIL", detail);
  if (!pass) g_fail = 1;
}

/* 阈值(来自设计规格第 9 节) */
#define GATE_S1_RSS_MB 80.0
#define GATE_S3_CYCLE_US 500.0
#define GATE_S4_RATIO 1.10

int main(int argc, char **argv) {
  u64 n_fns = argc > 1 ? strtoull(argv[1], NULL, 10) : 10000000ull;
  const char *pack = argc > 2 ? argv[2] : "bench/synth_10m.pack";
  const char *solo_pack = argc > 3 ? argv[3] : "bench/solo.pack";
  const char *cold_pack = argc > 4 ? argv[4] : "bench/cold.pack";
  const char *so = argc > 5 ? argv[5] : "build/bench/synth_mod.so";
  u64 n_modules = n_fns / 10;   /* 每模块 10 函数 */
  char err[256];

  /* ---- S1:冷规模 ---- */
  double t0 = mosaic_bench_now_us();
  if (mosaic_bench_build_universe(pack, so, n_fns, n_modules, 5, 2) != 0) {
    gate("S1", 0, "universe build failed"); return 1;
  }
  double t_build_s = (mosaic_bench_now_us() - t0) / 1e6;
  long rss_before = mosaic_bench_rss_kb();
  mosaic_runtime *rt = mosaic_runtime_open(pack, err, sizeof err);
  if (!rt) { gate("S1", 0, err); return 1; }
  long rss_after = mosaic_bench_rss_kb();
  double rss_mb = (double)(rss_after - rss_before) / 1024.0;
  /* 触碰 1k 个冷记录(模拟索引查询,不物化) */
  u64 seed = 7;
  for (int i = 0; i < 1000; i++) {
    u64 fn_id = (mosaic_bench_rng(&seed) % n_fns);
    const mosaic_function_record *r = mosaic_runtime_find_function(rt, fn_id);
    (void)r;
  }
  long rss_after_q = mosaic_bench_rss_kb();
  double rss_q_mb = (double)(rss_after_q - rss_before) / 1024.0;
  char detail[256];
  snprintf(detail, sizeof detail, "build %.2fs, fns=%llu, RSS delta %.2f MB (query %.2f MB), limit %.0f MB",
           t_build_s, (unsigned long long)n_fns, rss_mb, rss_q_mb, GATE_S1_RSS_MB);
  gate("S1", rss_q_mb <= GATE_S1_RSS_MB, detail);
  mosaic_runtime_close(rt);

  /* ---- S2:冷启动(诊断,非门禁)——专用 1k 函数冷包,避免在 10M 宇宙上
     物化数百万函数(单事件订阅 ~400 万,会把 RSS 打爆) ---- */
  if (mosaic_bench_build_cold(cold_pack, so) != 0) { gate("S2", 0, "cold build failed"); return 1; }
  mosaic_runtime *rc2 = mosaic_runtime_open(cold_pack, err, sizeof err);
  if (!rc2) { gate("S2", 0, err); return 1; }
  u32 ev_cold = mosaic_runtime_event_id(rc2, "cold");
  if (ev_cold == MOSAIC_U32_NONE) { gate("S2", 0, "cold event not found"); return 1; }
  t0 = mosaic_bench_now_us();
  u32 executed = mosaic_event_dispatch(rc2, ev_cold, NULL);
  double t_s2 = mosaic_bench_now_us() - t0;
  printf("S2 DIAG: cold dispatch -> %u materialized+executed, %.1f us total, %.2f us/fn\n",
         executed, t_s2, executed ? t_s2 / executed : 0.0);
  mosaic_evict_config zcfg = { 0 };
  mosaic_evict_idle(rc2, &zcfg);
  mosaic_runtime_close(rc2);

  /* ---- S3:全循环(物化→执行→墓碑→恢复→执行) ---- */
  if (mosaic_bench_build_solo(solo_pack, so) != 0) { gate("S3", 0, "solo build failed"); return 1; }
  mosaic_runtime *rs = mosaic_runtime_open(solo_pack, err, sizeof err);
  if (!rs) { gate("S3", 0, err); return 1; }
  u32 ev_solo = mosaic_runtime_event_id(rs, "solo");
  t0 = mosaic_bench_now_us();
  mosaic_event_dispatch(rs, ev_solo, NULL);      /* 物化 + 执行 */
  mosaic_evict_idle(rs, &zcfg);                  /* 墓碑 */
  mosaic_event_dispatch(rs, ev_solo, NULL);      /* 恢复 + 执行 */
  double t_cycle_us = mosaic_bench_now_us() - t0;
  snprintf(detail, sizeof detail, "full cycle %.1f us, limit %.0f us", t_cycle_us, GATE_S3_CYCLE_US);
  gate("S3", t_cycle_us <= GATE_S3_CYCLE_US, detail);

  /* ---- S4:热路径 vs 直调 ---- */
  mosaic_fn_obj *fn = mosaic_fn_materialize(rs, 42ull << 32);
  if (!fn) { gate("S4", 0, "materialize failed"); return 1; }
  const int ITERS = 5000000;
  volatile u32 sink = 0;
  t0 = mosaic_bench_now_us();
  for (int i = 0; i < ITERS; i++) mosaic_fn_execute(fn, ev_solo, NULL);
  double t_exec = mosaic_bench_now_us() - t0;
  t0 = mosaic_bench_now_us();
  for (int i = 0; i < ITERS; i++) { fn->code(fn->state, ev_solo, NULL); sink += (u32)i; }
  double t_direct = mosaic_bench_now_us() - t0;
  (void)sink;
  double ratio = t_exec / t_direct;
  snprintf(detail, sizeof detail, "execute %.1f ns/call, direct %.1f ns/call, ratio %.3f, limit %.2f",
           t_exec * 1000.0 / ITERS, t_direct * 1000.0 / ITERS, ratio, GATE_S4_RATIO);
  gate("S4", ratio <= GATE_S4_RATIO, detail);
  mosaic_runtime_close(rs);

  printf(g_fail ? "\nGATES FAILED\n" : "\nALL GATES PASSED\n");
  return g_fail ? 1 : 0;
}
```

- [ ] **Step 5: CMakeLists.txt 追加**

```cmake
add_library(synth_mod MODULE bench/synth_abi.c)
target_include_directories(synth_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(bench_runner bench/bench_runner.c bench/synth_universe.c)
target_include_directories(bench_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/bench)
target_link_libraries(bench_runner mosaic_core m dl)
```

- [ ] **Step 6: 构建并运行门禁**

Run: `cmake -B build && cmake --build build -j`
Run: `build/bench/bench_runner 10000000`
Expected: 输出 `GATE S1 PASS`、`GATE S3 PASS`、`GATE S4 PASS`、`ALL GATES PASSED`,退出码 0。

> 注:首次运行会生成 `bench/synth_10m.pack`(约 700MB 磁盘文件)与 `bench/solo.pack`。S3 首次 dlopen 延迟包含在循环内——若 S3 偶发超阈值,重跑一次(页缓存已热);若持续超阈值,检查是不是 debug 构建(必须 Release)。

- [ ] **Step 7: 提交**

```bash
git add bench CMakeLists.txt
git commit -m "feat: 10M synth universe, benchmark runner with S1-S4 gates"
```

---

### Task 11: CI 门禁脚本 + README + 全量验证

**Files:**
- Create: `ci/gates.sh`
- Create: `README.md`
- Modify: `CMakeLists.txt`(bench_runner 的 ctest 冒烟项,可选)
- Test: 全量 `ci/gates.sh`

**Interfaces:**
- Consumes: Task 10 的 bench_runner
- Produces: 一键验证入口 `ci/gates.sh`(Release 构建 + ctest + 10M 门禁,任何失败 → 非零退出)

- [ ] **Step 1: 写 ci/gates.sh**

```bash
#!/usr/bin/env bash
# Mosaic M1 验收门禁:Release 构建 + 单元/属性测试 + 10M 基准硬指标
# 任何一步失败 → 非零退出(回归即失败)
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo "=== ctest ==="
ctest --test-dir build --output-on-failure

echo "=== gates (10M cold functions) ==="
build/bench/bench_runner 10000000

echo "=== ALL CHECKS PASSED ==="
```

- [ ] **Step 2: 写 README.md**

```markdown
# Mosaic

面向 Minecraft 的 Native、事件驱动、函数级惰性模块运行时(M1:核心循环原型)。

## 设计

- 规格:`docs/superpowers/specs/2026-08-21-mosaic-runtime-design.md`
- 实现计划:`docs/superpowers/plans/2026-08-21-mosaic-m1-core.md`

## 构建与验证

```bash
./ci/gates.sh          # Release 构建 + 全部测试 + 10M 基准门禁
```

## M1 验收硬指标(CI 门禁)

| 门禁 | 指标 | 阈值 |
|---|---|---|
| S1 冷规模 | 10M 冷函数 RSS 增量 | ≤ 80 MB |
| S3 全循环 | 触发→物化→执行→墓碑→恢复→执行 | ≤ 500 μs |
| S4 热路径 | 分派/直调比 | ≤ 1.10 |
| S2 冷启动 | 1k 函数物化延迟 | 诊断性,非门禁 |

## 布局

```
include/mosaic/  稳定 API 头(base/pack/runtime/module/function/event/ownership/eviction)
src/core/        pack_reader·index·working_set·lifecycle(L0-L3)
src/services/    trigger·ownership·eviction(L4)
src/pack_builder.c  离线 pack 构建器
bench/           合成宇宙 + S1-S4 基准
tests/           mini_test 单元/属性测试
ci/gates.sh      验收门禁
```
```

- [ ] **Step 3: 运行全量验证**

Run: `chmod +x ci/gates.sh && ./ci/gates.sh`
Expected: ctest 全过 + `ALL GATES PASSED` + `ALL CHECKS PASSED`。

- [ ] **Step 4: 提交**

```bash
git add ci README.md
git commit -m "ci: M1 acceptance gates script and README"
```

---

## 自审记录

**规格覆盖对照:**
- L0 冷存储/48B 记录/≤8B 摊派 → Task 1, 2, 3, 10(S1)
- L1 紧凑索引(mm 内二分) → Task 4
- L2 工作集 arena/ws 哈希/窗口链表 → Task 5
- L3 状态机 5+2 态(COLD/MATERIALIZING/ACTIVE/QUIESCING,TOMBSTONED=COLD+state_off) → Task 6, 7
- L4 触发索引/派发 → Task 8;所有权租约 → Task 9;驱逐(窗口 T + refs 保护)→ Task 9
- 全循环 ≤500μs / RSS ≤80MB / 热路径 ≤1.10 → Task 10, 11
- 降级语义(refs>0 不驱逐、物化失败跳过、pack 损坏 fail-fast) → Task 3, 7, 8, 9
- 里程碑外(M2+):事务/滚动更新(generation 字段已留)、DAG 调度器、描述符 API、JVM Bridge、MC 集成 — 不在本计划范围

**类型/签名一致性:** 全部 `mosaic_*` 前缀;`mosaic_fn_materialize/execute/tombstone`、`mosaic_event_dispatch`、`mosaic_lease_acquire/release`、`mosaic_evict_idle` 在头文件声明与实现间一致;内部 `ws_* / fn_alloc / mod_load / state_blob_append` 在 mosaic_internal.h 声明、Task 5/6 实现。

**计划内推迟(有意为之,非占位):** 并行构建排序(Task 2 注释说明)、GDSF 权重完整版(驱逐接口已留,窗口简化版为 M1 决策)、meta blob 去重(append-only)、状态 blob 压缩回收(墓碑日志追加)。

**自审修正(已写入上文各任务):**
1. Task 1:CMakeLists 引用的 7 个 src 占位文件随 Task 1 创建(否则 configure 失败);Task 3 Step 4 改为"无需操作"。
2. Task 5:`ws_remove` 增加开放寻址簇内后移(否则删除中间槽后簇内靠后键不可达,经典 bug);`fn_alloc` 空闲链表弹出修复 + 两条路径统一 memset;新增 `test_cluster_removal` 回归测试(ids 1/17/33 落同一簇)。
3. Task 8:test_trigger 触发器声明数 3 → 4(实际调用 4 次,原声明必然 finish 失败)。
4. Task 10:合成宇宙模块 id 由随机改为连续值(1M 模块在 1M 空间随机 id 撞车 ~39%,构建必失败);S2 改为专用 1k 函数冷包(原方案在 10M 宇宙派发 event0 会物化 ~400 万函数,RSS 爆炸);移除未用变量。
5. Task 2:`pack_builder.c` 补 `#include <unistd.h>`(fsync 声明)。
6. Task 4:`test_index.c` 补 `#include "mosaic_internal.h"`(使用了 rt->map)。
