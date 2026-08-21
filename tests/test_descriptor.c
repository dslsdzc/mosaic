/* tests/test_descriptor.c — M2-4:item 描述符表(pack v3)+ 冷态查询
 *
 * - pack A:2 模块(so 路径不存在!)、3 个 fn、3 分类 × 7 条 item,名字含
 *   前缀对 "sword"/"sword_gold"/"sword_iron" 验证二分长度感知比较;
 * - pack B:1 模块、3 条 item(cat 1 的 "apple" 与 pack A 重名 → 跨 pack
 *   by_name 以 pack 顺序为准);
 * - 断言:item_count、by_name 命中/未命中(前缀对 + 长度 tiebreak)、
 *   for_each 分类区间完整、字符串读取(name/tags/icon)、重复名字 → finish
 *   拒绝 "duplicate item name"、损坏 item 表 → open 失败;
 * - 不物化验证:查询后物化 provider → ABI 失败(so 不存在),自证查询阶段
 *   零 dlopen——查询成功本身即证明未触碰 so(触碰即失败)。 */
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/descriptor.h"
#include "mosaic/function.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PA_PATH "/tmp/mosaic_desc_pa.pack"
#define PB_PATH "/tmp/mosaic_desc_pb.pack"

/* pack A:模块 10/20,fn 10|0, 10|1, 20|0;item provider 全部指向这些 fn。
   模块 so_path 故意指向不存在的路径(不物化验证的前提)。 */
static int build_pack_a(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(PA_PATH, 2, 3, 0, 0, 0);
  if (!b) return -1;
  if (mosaic_pack_builder_set_item_count(b, 8) != 0) { mosaic_pack_builder_free(b); return -1; }
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", "/nonexistent/mosaic_desc_a.so");
  mosaic_pack_builder_add_module(b, 20, 1, "mod_b", "/nonexistent/mosaic_desc_b.so");
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 0, 1, 1, 0);
  mosaic_pack_builder_add_fn(b, 10, 1, 1, 0, 1, 1, 0);
  mosaic_pack_builder_add_fn(b, 20, 0, 0, 0, 1, 1, 0);
  /* cat 0:5 条,含前缀对 sword / sword_gold / sword_iron */
  mosaic_pack_builder_add_item(b, 10ull << 32 | 0, "dirt", "block", 0, "block/dirt", 1);
  mosaic_pack_builder_add_item(b, 10ull << 32 | 0, "stone", NULL, 0, "block/stone", 0);
  mosaic_pack_builder_add_item(b, 10ull << 32 | 1, "sword", "weapon,melee", 0, "item/sword", 2);
  mosaic_pack_builder_add_item(b, 20ull << 32 | 0, "sword_gold", "weapon,melee,gold", 0, "item/sword_gold", 2);
  mosaic_pack_builder_add_item(b, 10ull << 32 | 1, "sword_iron", "weapon,melee,iron", 0, "item/sword_iron", 2);
  /* cat 1:2 条(bread 无 tags/icon → 字符串读取返回 NULL) */
  mosaic_pack_builder_add_item(b, 10ull << 32 | 0, "apple", "food", 1, "item/apple", 0);
  mosaic_pack_builder_add_item(b, 10ull << 32 | 0, "bread", NULL, 1, NULL, 0);
  /* cat 2:1 条 */
  mosaic_pack_builder_add_item(b, 20ull << 32 | 0, "zombie_spawn_egg", NULL, 2, NULL, 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "build A: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

/* pack B:模块 30,fn 30|0;3 条 item。cat 1 的 "apple" 与 pack A 重名(跨 pack
   合法,by_name 以 pack 顺序为准)。 */
static int build_pack_b(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create(PB_PATH, 1, 1, 0, 0, 0);
  if (!b) return -1;
  if (mosaic_pack_builder_set_item_count(b, 3) != 0) { mosaic_pack_builder_free(b); return -1; }
  mosaic_pack_builder_add_module(b, 30, 1, "mod_c", "/nonexistent/mosaic_desc_c.so");
  mosaic_pack_builder_add_fn(b, 30, 0, 0, 0, 1, 1, 0);
  mosaic_pack_builder_add_item(b, 30ull << 32 | 0, "apple", "food,red", 1, "item/apple_b", 0);
  mosaic_pack_builder_add_item(b, 30ull << 32 | 0, "cake", "food", 1, "item/cake", 0);
  mosaic_pack_builder_add_item(b, 30ull << 32 | 0, "creeper_spawn_egg", NULL, 2, NULL, 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "build B: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_item_accessors(void) {
  MT_CHECK(sizeof(mosaic_item_record) == 32);
  mosaic_item_record r; memset(&r, 0, sizeof r);
  mi_set_provider(&r, 0x1122334455667788ull);
  mi_set_name_off(&r, 100); mi_set_tags_off(&r, 200);
  mi_set_category(&r, 7); mi_set_icon_off(&r, 300); mi_set_flags(&r, 0xDEADBEEF);
  MT_CHECK_EQ_U64(mi_provider(&r), 0x1122334455667788ull);
  MT_CHECK_EQ_U64(mi_name_off(&r), 100); MT_CHECK_EQ_U64(mi_tags_off(&r), 200);
  MT_CHECK_EQ_U64(mi_category(&r), 7); MT_CHECK_EQ_U64(mi_icon_off(&r), 300);
  MT_CHECK_EQ_U64(mi_flags(&r), 0xDEADBEEFu);
  /* 字节偏移:provider 低字节在前(小端) */
  MT_CHECK(r.bytes[IT_OFF_PROVIDER + 0] == 0x88 && r.bytes[IT_OFF_PROVIDER + 7] == 0x11);
}

/* 落盘布局:item 表在 event_names 与 state 之间;按 (category, name) 排序
   (长度感知:sword < sword_gold < sword_iron);meta 字符串可读 */
static void test_pack_layout(void) {
  MT_CHECK(build_pack_a() == 0);
  FILE *f = fopen(PA_PATH, "rb");
  MT_CHECK(f != NULL);
  if (!f) return;
  u8 hdr[HDR_SIZE]; MT_CHECK(fread(hdr, 1, HDR_SIZE, f) == HDR_SIZE);
  MT_CHECK_EQ_U64(hdr_version(hdr), MOSAIC_PACK_VERSION);
  MT_CHECK_EQ_U64(hdr_item_count(hdr), 8);
  /* 布局:item 表紧随 event_names(此处 0 事件 → 紧随 meta) */
  MT_CHECK_EQ_U64(hdr_item_off(hdr), hdr_event_names_off(hdr) + (u64)hdr_event_count(hdr) * MN_SIZE);
  MT_CHECK_EQ_U64(hdr_state_off(hdr), hdr_item_off(hdr) + 8ull * IT_SIZE);

  fseek(f, (long)hdr_item_off(hdr), SEEK_SET);
  mosaic_item_record items[8];
  MT_CHECK(fread(items, 1, sizeof items, f) == sizeof items);
  /* 排序:(category, name) 升序;cat 0 内前缀对按长度感知序
     (sword < sword_gold < sword_iron,同 strcmp 序) */
  static const char *want_name[8] = { "dirt", "stone", "sword", "sword_gold", "sword_iron",
                                      "apple", "bread", "zombie_spawn_egg" };
  static const u32 want_cat[8] = { 0, 0, 0, 0, 0, 1, 1, 2 };
  static const char *want_tags[8] = { "block", NULL, "weapon,melee", "weapon,melee,gold",
                                      "weapon,melee,iron", "food", NULL, NULL };
  /* 逐条:名字经 meta blob 与预期一致 */
  fseek(f, (long)hdr_meta_off(hdr), SEEK_SET);
  u8 *meta = malloc((size_t)hdr_meta_len(hdr));
  MT_CHECK(meta != NULL);
  if (meta) {
    MT_CHECK(fread(meta, 1, (size_t)hdr_meta_len(hdr), f) == hdr_meta_len(hdr));
    for (int i = 0; i < 8; i++) {
      MT_CHECK_EQ_U64(mi_category(&items[i]), want_cat[i]);
      MT_CHECK(strcmp((const char *)meta + mi_name_off(&items[i]), want_name[i]) == 0);
      if (want_tags[i])
        MT_CHECK(strcmp((const char *)meta + mi_tags_off(&items[i]), want_tags[i]) == 0);
      else
        MT_CHECK_EQ_U64(mi_tags_off(&items[i]), 0);
    }
    free(meta);
  }
  fclose(f);
}

static void test_by_name(void) {
  char err[256];
  MT_CHECK(build_pack_a() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(PA_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) { fprintf(stderr, "open: %s\n", err); return; }

  /* 命中(含前缀对:精确匹配,不吞 sword_iron/sword_gold) */
  const mosaic_item_record *it = mosaic_item_by_name(rt, 0, "sword");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK_EQ_U64(mi_provider(it), 10ull << 32 | 1);
    MT_CHECK(strcmp(mosaic_item_name(rt, it), "sword") == 0);
    MT_CHECK(strcmp(mosaic_item_tags(rt, it), "weapon,melee") == 0);
    MT_CHECK(strcmp(mosaic_item_icon(rt, it), "item/sword") == 0);
    MT_CHECK_EQ_U64(mi_flags(it), 2);
  }
  it = mosaic_item_by_name(rt, 0, "sword_iron");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK_EQ_U64(mi_provider(it), 10ull << 32 | 1);
    MT_CHECK(strcmp(mosaic_item_name(rt, it), "sword_iron") == 0);
  }
  it = mosaic_item_by_name(rt, 0, "sword_gold");
  MT_CHECK(it != NULL);
  if (it) MT_CHECK_EQ_U64(mi_provider(it), 20ull << 32 | 0);
  it = mosaic_item_by_name(rt, 0, "dirt");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK_EQ_U64(mi_flags(it), 1);
    MT_CHECK(strcmp(mosaic_item_tags(rt, it), "block") == 0);
  }

  /* 未命中:不存在的名字;真前缀(长度 tiebreak → swordi 不是 sword_iron);
     名字存在但分类不对(分类区间隔离) */
  MT_CHECK(mosaic_item_by_name(rt, 0, "pear") == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  MT_CHECK(mosaic_item_by_name(rt, 0, "swordi") == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  MT_CHECK(mosaic_item_by_name(rt, 2, "apple") == NULL);      /* apple 在 cat 1 */
  MT_CHECK(mosaic_item_by_name(rt, 0, "") == NULL);           /* 空名不匹配任何条目 */
  /* NULL 参数防御 */
  MT_CHECK(mosaic_item_by_name(rt, 0, NULL) == NULL);
  MT_CHECK(mosaic_item_by_name(NULL, 0, "sword") == NULL);

  /* 空 tags/icon → 字符串读取 NULL */
  it = mosaic_item_by_name(rt, 1, "bread");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK(mosaic_item_name(rt, it) != NULL);
    MT_CHECK(mosaic_item_tags(rt, it) == NULL);
    MT_CHECK(mosaic_item_icon(rt, it) == NULL);
  }
  mosaic_runtime_close(rt);
}

struct collect_ctx { u64 providers[16]; const char *names[16]; size_t len; };
static int collect_cb(const mosaic_item_record *item, void *user) {
  struct collect_ctx *c = user;
  if (c->len < 16) { c->providers[c->len] = mi_provider(item); c->names[c->len] = NULL; }
  c->len++;
  return 0;
}
static int first_cb(const mosaic_item_record *item, void *user) { *(u64 *)user = mi_provider(item); return 1; }

static void test_for_each(void) {
  char err[256];
  MT_CHECK(build_pack_a() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(PA_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;

  struct collect_ctx c;
  /* cat 0 完整区间:5 条 */
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 0, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 5);
  MT_CHECK_EQ_U64(c.providers[0], 10ull << 32 | 0);   /* dirt */
  MT_CHECK_EQ_U64(c.providers[2], 10ull << 32 | 1);   /* sword */
  MT_CHECK_EQ_U64(c.providers[3], 20ull << 32 | 0);   /* sword_gold */
  MT_CHECK_EQ_U64(c.providers[4], 10ull << 32 | 1);   /* sword_iron */
  /* cat 1 / cat 2 区间 */
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 1, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 2);
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 2, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 1);
  /* 无条目分类 → 零回调 */
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 9, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 0);
  /* 回调非 0 → 停止并透传(首条即停) */
  u64 first = 0;
  MT_CHECK(mosaic_item_for_each(rt, 0, first_cb, &first) == 1);
  MT_CHECK_EQ_U64(first, 10ull << 32 | 0);
  /* NULL 参数 */
  MT_CHECK(mosaic_item_for_each(rt, 0, NULL, NULL) == -1);
  MT_CHECK(mosaic_item_for_each(NULL, 0, collect_cb, &c) == -1);
  mosaic_runtime_close(rt);
}

static void test_multi_pack(void) {
  char err[256];
  MT_CHECK(build_pack_a() == 0 && build_pack_b() == 0);
  const char *paths[2] = { PA_PATH, PB_PATH };
  mosaic_runtime *rt = mosaic_runtime_open_many(paths, 2, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) { fprintf(stderr, "open_many: %s\n", err); return; }

  /* 跨 pack 总数 = 8 + 3 */
  MT_CHECK_EQ_U64(mosaic_item_count(rt), 11);
  MT_CHECK_EQ_U64(mosaic_item_count(NULL), 0);

  /* 重名 apple 在 pack A 与 pack B:by_name 以 pack 顺序为准 → A 的 apple */
  const mosaic_item_record *it = mosaic_item_by_name(rt, 1, "apple");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK_EQ_U64(mi_provider(it), 10ull << 32 | 0);
    MT_CHECK(strcmp(mosaic_item_tags(rt, it), "food") == 0);     /* A 的 tags */
  }
  /* 只存在于 pack B 的条目:跨 pack 命中 + 跨 pack 字符串读取(uintptr 扫描) */
  it = mosaic_item_by_name(rt, 2, "creeper_spawn_egg");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK_EQ_U64(mi_provider(it), 30ull << 32 | 0);
    MT_CHECK(strcmp(mosaic_item_name(rt, it), "creeper_spawn_egg") == 0);
  }
  it = mosaic_item_by_name(rt, 1, "cake");
  MT_CHECK(it != NULL);
  if (it) MT_CHECK(strcmp(mosaic_item_icon(rt, it), "item/cake") == 0);
  MT_CHECK(mosaic_item_by_name(rt, 0, "pear") == NULL);          /* 全局未命中 */

  /* 跨 pack 枚举:cat 1 = A{apple, bread} + B{apple, cake},顺序 pack 序 */
  struct collect_ctx c;
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 1, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 4);
  MT_CHECK_EQ_U64(c.providers[0], 10ull << 32 | 0);   /* apple A */
  MT_CHECK_EQ_U64(c.providers[1], 10ull << 32 | 0);   /* bread A */
  MT_CHECK_EQ_U64(c.providers[2], 30ull << 32 | 0);   /* apple B */
  MT_CHECK_EQ_U64(c.providers[3], 30ull << 32 | 0);   /* cake B */
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 2, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 2);                          /* zombie A + creeper B */
  mosaic_runtime_close(rt);
}

static void test_builder_rejects(void) {
  char err[256];
  /* 分类内重名 → finish 拒绝 */
  const char *p = "/tmp/mosaic_desc_dup.pack";
  mosaic_pack_builder *b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  MT_CHECK(b != NULL);
  if (!b) return;
  MT_CHECK(mosaic_pack_builder_set_item_count(b, 2) == 0);
  mosaic_pack_builder_add_item(b, 1, "sword", NULL, 0, NULL, 0);
  mosaic_pack_builder_add_item(b, 2, "sword", NULL, 0, NULL, 0);
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == -1);
  MT_CHECK(strstr(err, "duplicate item name") != NULL);
  mosaic_pack_builder_free(b);

  /* 同名字不同分类 → 合法(分类区间隔离,名字空间按 (category,name)) */
  b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  if (!b) return;
  MT_CHECK(mosaic_pack_builder_set_item_count(b, 2) == 0);
  mosaic_pack_builder_add_item(b, 1, "sword", NULL, 0, NULL, 0);
  mosaic_pack_builder_add_item(b, 2, "sword", NULL, 1, NULL, 0);
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == 0);
  mosaic_pack_builder_free(b);

  /* set_item_count 只可调用一次(在 add_item 之前) */
  b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  if (!b) return;
  MT_CHECK(mosaic_pack_builder_set_item_count(b, 1) == 0);
  MT_CHECK(mosaic_pack_builder_set_item_count(b, 1) == -1);   /* 已设置 */
  mosaic_pack_builder_free(b);

  /* add_item 先于 set_item_count → failed,finish 拒绝 */
  b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  if (!b) return;
  mosaic_pack_builder_add_item(b, 1, "sword", NULL, 0, NULL, 0);
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == -1);
  MT_CHECK(strstr(err, "record count mismatch") != NULL);
  mosaic_pack_builder_free(b);

  /* 声明 3 条只填 2 条 → 计数不符 */
  b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  if (!b) return;
  MT_CHECK(mosaic_pack_builder_set_item_count(b, 3) == 0);
  mosaic_pack_builder_add_item(b, 1, "a", NULL, 0, NULL, 0);
  mosaic_pack_builder_add_item(b, 2, "b", NULL, 0, NULL, 0);
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == -1);
  mosaic_pack_builder_free(b);
}

/* 损坏 item 表(offset/count 越界)→ open 失败(BAD_PACK) */
static void test_corrupt_item_table(void) {
  char err[256];
  MT_CHECK(build_pack_a() == 0);
  /* offset 越界:item_off 指向映射外(文件只有几百字节) */
  {
    FILE *f = fopen(PA_PATH, "r+b");
    MT_CHECK(f != NULL);
    if (f) {
      u8 v[8]; wr_le64(v, 1ull << 40);
      MT_CHECK(fseek(f, HDR_ITEM_OFF, SEEK_SET) == 0);
      MT_CHECK(fwrite(v, 1, 8, f) == 8);
      fclose(f);
    }
    mosaic_runtime *rt = mosaic_runtime_open(PA_PATH, err, sizeof err);
    MT_CHECK(rt == NULL);
    if (rt) mosaic_runtime_close(rt);
  }
  /* count 越界:count×IT_SIZE 超出映射(除法防回绕路径) */
  {
    FILE *f = fopen(PA_PATH, "r+b");
    MT_CHECK(f != NULL);
    if (f) {
      u8 v[8]; wr_le64(v, 1ull << 40);
      MT_CHECK(fseek(f, HDR_ITEM_OFF, SEEK_SET) == 0);
      MT_CHECK(fwrite(v, 1, 8, f) == 8);       /* 复原 off */
      wr_le64(v, 1ull << 20);
      MT_CHECK(fseek(f, HDR_ITEM_COUNT, SEEK_SET) == 0);
      MT_CHECK(fwrite(v, 1, 8, f) == 8);
      fclose(f);
    }
    mosaic_runtime *rt = mosaic_runtime_open(PA_PATH, err, sizeof err);
    MT_CHECK(rt == NULL);
    if (rt) mosaic_runtime_close(rt);
  }
}

/* 空 item 表:count=0 → 查询返回空,open 正常 */
static void test_empty_item_table(void) {
  char err[256];
  const char *p = "/tmp/mosaic_desc_empty.pack";
  mosaic_pack_builder *b = mosaic_pack_builder_create(p, 0, 0, 0, 0, 0);
  MT_CHECK(b != NULL);
  if (!b) return;
  MT_CHECK(mosaic_pack_builder_finish(b, err, sizeof err) == 0);
  mosaic_pack_builder_free(b);
  mosaic_runtime *rt = mosaic_runtime_open(p, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  MT_CHECK_EQ_U64(mosaic_item_count(rt), 0);
  MT_CHECK(mosaic_item_by_name(rt, 0, "anything") == NULL);
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_NOT_FOUND);
  struct collect_ctx c;
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 0, collect_cb, &c) == 0);     /* 空表不回调 */
  MT_CHECK_EQ_U64(c.len, 0);
  mosaic_runtime_close(rt);
}

/* 不物化验证:查询全程不 dlopen(模块 so 不存在 → 查询成功即零加载自证);
   物化 provider → ABI 失败(预期的唯一失败点)。 */
static void test_no_materialization(void) {
  char err[256];
  MT_CHECK(build_pack_a() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(PA_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;

  /* 冷态查询全成功(so 不存在,任何加载尝试都会失败 → 查询没碰过 so) */
  MT_CHECK_EQ_U64(mosaic_item_count(rt), 8);
  const mosaic_item_record *it = mosaic_item_by_name(rt, 0, "sword");
  MT_CHECK(it != NULL);
  if (it) {
    MT_CHECK(mosaic_item_name(rt, it) != NULL);
    MT_CHECK(mosaic_item_tags(rt, it) != NULL);
    MT_CHECK(mosaic_item_icon(rt, it) != NULL);
  }
  struct collect_ctx c;
  memset(&c, 0, sizeof c);
  MT_CHECK(mosaic_item_for_each(rt, 0, collect_cb, &c) == 0);
  MT_CHECK_EQ_U64(c.len, 5);

  /* 真正拿出 Item 才物化:provider so 不存在 → ABI 失败(证明加载只在物化,
     查询阶段零加载) */
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, mi_provider(it));
  MT_CHECK(fn == NULL);
  if (fn) { MT_CHECK(0); mosaic_fn_tombstone(rt, fn); }
  MT_CHECK_EQ_U64(mosaic_runtime_last_error(rt), MOSAIC_ERR_ABI);
  /* 物化失败后查询仍正常(无残留状态) */
  MT_CHECK(mosaic_item_by_name(rt, 0, "sword_iron") != NULL);
  mosaic_runtime_close(rt);
}

int main(void) {
  MT_RUN(test_item_accessors);
  MT_RUN(test_pack_layout);
  MT_RUN(test_by_name);
  MT_RUN(test_for_each);
  MT_RUN(test_multi_pack);
  MT_RUN(test_builder_rejects);
  MT_RUN(test_corrupt_item_table);
  MT_RUN(test_empty_item_table);
  MT_RUN(test_no_materialization);
  return MT_RESULT() ? 0 : 1;
}
