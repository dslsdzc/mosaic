/* tests/test_events.c — M3-1:事件类型 API v1。
   目录完整性(规模/唯一/排序/频率档/载荷签名)+ 二分查找 + 大事件集 pack
   全链路(300 事件构建 → 打开 → 查找 → 派发执行订阅者 → 关闭;
   验证 MOSAIC_MAX_EVENTS 64→4096 放宽后构建/查找/派发仍正确)。
   大事件集用例需要 test_mod.so fixture(CMake 传入)。 */
#include "mosaic/base.h"
#include "mosaic/events.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/function.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SO_PATH;

#define PACK_PATH "/tmp/mosaic_test_ev300.pack"

/* 长度感知比较(与目录排序/查找同一语义:memcmp 前缀 + 长度 tiebreak;
   名字唯一时与 strcmp 序一致) */
static int ev_cmp(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t c = la < lb ? la : lb;
  int r = memcmp(a, b, c);
  if (r) return r;
  return (int)la - (int)lb;
}

/* ---- 目录完整性:规模 ≥ 200、名字唯一、按名排序、小写蛇形、频率档合法 ---- */
static void test_catalog_integrity(void) {
  MT_CHECK(mosaic_events_catalog_count >= 200);
  for (u32 i = 0; i < mosaic_events_catalog_count; i++) {
    const mosaic_ev_spec *s = &mosaic_events_catalog[i];
    MT_CHECK(s->name != NULL && s->name[0] != '\0');
    /* 小写蛇形(允许纯数字/下划线外的字符为小写字母) */
    for (const char *p = s->name; *p; p++)
      MT_CHECK((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_');
    /* 频率档 ∈ {LOW, MID, HIGH} */
    MT_CHECK(s->freq >= MOSAIC_EV_FREQ_LOW && s->freq <= MOSAIC_EV_FREQ_HIGH);
    /* 载荷不超过最大域结构体(实体域 20B);0 表示无载荷 */
    MT_CHECK(s->payload_size == 0 || s->payload_size <= sizeof(mosaic_ev_entity));
    /* 排序 + 唯一:相邻条目按长度感知序严格递增(排序是二分前提,唯一是
       builder 重名拒绝的对偶要求) */
    if (i > 0) MT_CHECK(ev_cmp(mosaic_events_catalog[i - 1].name, s->name) < 0);
  }
}

/* ---- 载荷签名抽查:每域代表事件的 payload_size 与域结构体一致 ---- */
static void test_catalog_payload_sizes(void) {
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("block_break")->payload_size, sizeof(mosaic_ev_block));
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("player_join")->payload_size, sizeof(mosaic_ev_player));
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("item_use")->payload_size, sizeof(mosaic_ev_item));
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("entity_spawn")->payload_size, sizeof(mosaic_ev_entity));
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("tick")->payload_size, sizeof(mosaic_ev_tick));
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("server_start")->payload_size, 0);   /* 无载荷 */
  MT_CHECK_EQ_U64(mosaic_event_spec_by_name("server_stop")->payload_size, 0);
  /* 对齐 4 字节:所有域结构体大小为 4 的倍数 */
  MT_CHECK_EQ_U64(sizeof(mosaic_ev_player) % 4, 0);
  MT_CHECK_EQ_U64(sizeof(mosaic_ev_block) % 4, 0);
  MT_CHECK_EQ_U64(sizeof(mosaic_ev_item) % 4, 0);
  MT_CHECK_EQ_U64(sizeof(mosaic_ev_entity) % 4, 0);
  MT_CHECK_EQ_U64(sizeof(mosaic_ev_tick) % 4, 0);
  /* 载荷大小(非零)与 4 对齐 */
  for (u32 i = 0; i < mosaic_events_catalog_count; i++)
    if (mosaic_events_catalog[i].payload_size)
      MT_CHECK_EQ_U64(mosaic_events_catalog[i].payload_size % 4, 0);
}

/* ---- 二分查找:首/尾/中/不存在(前缀名与乱造名)/NULL ---- */
static void test_catalog_lookup(void) {
  const mosaic_ev_spec *first = &mosaic_events_catalog[0];
  const mosaic_ev_spec *last = &mosaic_events_catalog[mosaic_events_catalog_count - 1];
  /* 首/尾 */
  MT_CHECK(mosaic_event_spec_by_name(first->name) == first);
  MT_CHECK(mosaic_event_spec_by_name(last->name) == last);
  /* 中(每 16 条抽一个) */
  for (u32 i = 0; i < mosaic_events_catalog_count; i += 16)
    MT_CHECK(mosaic_event_spec_by_name(mosaic_events_catalog[i].name) == &mosaic_events_catalog[i]);
  /* 不存在:前缀名(长度感知序下"短于完整名"必然未命中)与乱造名 */
  MT_CHECK(mosaic_event_spec_by_name("block_anvil") == NULL);       /* 前缀 */
  MT_CHECK(mosaic_event_spec_by_name("player_joi") == NULL);        /* 前缀 */
  MT_CHECK(mosaic_event_spec_by_name("zzz_not_an_event") == NULL);  /* 超出目录 */
  MT_CHECK(mosaic_event_spec_by_name("") == NULL);
  MT_CHECK(mosaic_event_spec_by_name(NULL) == NULL);
  /* 命中项字段与目录一致 */
  const mosaic_ev_spec *bb = mosaic_event_spec_by_name("block_break");
  MT_CHECK(bb != NULL && bb->freq == MOSAIC_EV_FREQ_HIGH);
  const mosaic_ev_spec *sj = mosaic_event_spec_by_name("server_start");
  MT_CHECK(sj != NULL && sj->freq == MOSAIC_EV_FREQ_LOW);
}

/* ---- 大事件集 pack:整个目录(205 个事件名,≫ 旧上限 64)构建 → 打开 →
     查找命中 → 派发执行订阅者 → 关闭(MOSAIC_MAX_EVENTS 放宽后全链路) ---- */
static int build_big_pack(void) {
  char err[256];
  const u32 n = mosaic_events_catalog_count;
  mosaic_pack_builder *b = mosaic_pack_builder_create(PACK_PATH, 1, 1, 1, 0, n);
  if (!b) return -1;
  for (u32 i = 0; i < n; i++)
    mosaic_pack_builder_add_event(b, mosaic_events_catalog[i].name);
  mosaic_pack_builder_add_module(b, 10, 1, "mod_a", SO_PATH);
  /* fn 订阅事件 0(注册序 == 目录排序序 → finish 后 id 不变) */
  mosaic_pack_builder_add_fn(b, 10, 0, 0, 64, 1, 0, MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 10ull << 32 | 0);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "finish: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc;
}

static void test_big_pack_lookup_and_dispatch(void) {
  char err[256];
  MT_CHECK(build_big_pack() == 0);
  mosaic_runtime *rt = mosaic_runtime_open(PACK_PATH, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 抽样 10+ 个事件名:目录排序序 == pack 内 id(注册序 == 排序序),命中必须
     一致——同时验证运行时二分在 205 事件规模下正确 */
  for (u32 i = 0; i < mosaic_events_catalog_count; i += 20) {
    u32 id = mosaic_runtime_event_id(rt, mosaic_events_catalog[i].name);
    MT_CHECK_EQ_U64(id, i);
  }
  /* 不存在的事件名 → MOSAIC_U32_NONE */
  MT_CHECK_EQ_U64(mosaic_runtime_event_id(rt, "not_in_catalog"), MOSAIC_U32_NONE);
  /* 派发目录首个事件(block_anvil_break,id 0):订阅者执行 1 次,
     载荷按域结构体传 mosaic_ev_block */
  mosaic_ev_block ev;
  memset(&ev, 0, sizeof ev);
  ev.player_id = 7; ev.x = 1; ev.y = 2; ev.z = 3; ev.block_type = 5;
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 0, &ev), 1);
  mosaic_fn_obj *f = mosaic_fn_materialize(rt, 10ull << 32);
  MT_CHECK(f != NULL);
  if (f) {
    MT_CHECK_EQ_U64(*(u32 *)f->state, 1);           /* counter == 1 */
    MT_CHECK_EQ_U64(((u32 *)f->state)[1], 0);       /* last_event == 0 */
  }
  /* 再派发另一个事件(id 90):无订阅 → 0 执行 */
  MT_CHECK_EQ_U64(mosaic_event_dispatch(rt, 90, NULL), 0);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <test_mod.so>\n", argv[0]); return 1; }
  SO_PATH = argv[1];
  MT_RUN(test_catalog_integrity);
  MT_RUN(test_catalog_payload_sizes);
  MT_RUN(test_catalog_lookup);
  MT_RUN(test_big_pack_lookup_and_dispatch);
  return MT_RESULT() ? 0 : 1;
}
