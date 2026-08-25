/* tests/test_packets.c — M6-E:包类型目录 v1(LC-2:内嵌变体补全)。
   目录完整性(规模/唯一/排序/分组 id 连续)+ 访问器(越界 null/负 index/
   逐项与目录同序)+ UNKNOWN 语义(0 不入目录)。
   包目录是纯常量表(无运行时注册态),测试零 fixture,纯静态断言。 */
#include "mosaic/base.h"
#include "mosaic/packets.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>

/* 分组基址(与 packets.h 注释一致的 1.20.1 锚定 id 空间;CONFIG 组 1.20.1
   为空,不参与断言) */
#define GRP_PLAY_IN   0x0100u
#define GRP_PLAY_OUT  0x0200u
#define GRP_LOGIN_IN  0x0500u
#define GRP_LOGIN_OUT 0x0600u
#define GRP_STATUS_IN 0x0700u
#define GRP_STATUS_OUT 0x0800u
#define GRP_HANDSHAKE 0x0900u

/* ---- 目录完整性:175 类(168 顶层 + 7 内嵌变体)、名字唯一、id 全非 0
     (UNKNOWN=0 不入目录)、id 唯一 ---- */
static void test_catalog_integrity(void) {
  MT_CHECK_EQ_U64(mosaic_packets_catalog_count, 175);
  for (u32 i = 0; i < mosaic_packets_catalog_count; i++) {
    const mosaic_packet_entry *e = &mosaic_packets_catalog[i];
    MT_CHECK(e->name != NULL && e->name[0] != '\0');
    MT_CHECK(e->id != 0);                      /* UNKNOWN=0 不入目录 */
    for (const char *p = e->name; *p; p++)
      MT_CHECK((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
               || (*p >= '0' && *p <= '9') || *p == '$');   /* LC-2:变体名含 '$' */
  }
  /* 排序纪律(LC-2):原始 168 条(非 '$' 名)按名升序(ASCII:strcmp 序 ==
     长度感知序);内嵌变体块(名字含 '$')追加在目录尾部、块内按名升序——
     '$' 名构成目录的连续后缀。断言派生自该结构(不硬编码边界):同块相邻
     名严格升序 + 块边界只允许一次 非'$' → '$' 迁移。 */
  int in_dollar_block = 0;
  for (u32 i = 0; i < mosaic_packets_catalog_count; i++) {
    int is_dollar = strchr(mosaic_packets_catalog[i].name, '$') != NULL;
    if (i == 0) { in_dollar_block = is_dollar; continue; }
    const char *prev = mosaic_packets_catalog[i - 1].name;
    const char *cur = mosaic_packets_catalog[i].name;
    if (is_dollar == in_dollar_block) {
      MT_CHECK(strcmp(prev, cur) < 0);
    } else {
      MT_CHECK(!in_dollar_block && is_dollar);   /* 恰一次块边界(后缀) */
      in_dollar_block = 1;
    }
  }
}

/* ---- 分组 id 连续:组内条目 id == 组基址 + 组内序号(组内顺序 = 目录序,
     即按名升序;条目出现即分配基址+1、+2…递增) ---- */
static void test_group_ids(void) {
  u32 in_play = 0, out_play = 0, in_login = 0, out_login = 0;
  u32 in_status = 0, out_status = 0, handshake = 0;
  for (u32 i = 0; i < mosaic_packets_catalog_count; i++) {
    const mosaic_packet_entry *e = &mosaic_packets_catalog[i];
    u32 id = e->id;
    if (id >= GRP_PLAY_IN && id < GRP_PLAY_IN + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_PLAY_IN + in_play + 1);
      MT_CHECK(strncmp(e->name, "Serverbound", 11) == 0);
      in_play++;
    } else if (id >= GRP_PLAY_OUT && id < GRP_PLAY_OUT + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_PLAY_OUT + out_play + 1);
      MT_CHECK(strncmp(e->name, "Clientbound", 11) == 0);
      out_play++;
    } else if (id >= GRP_LOGIN_IN && id < GRP_LOGIN_IN + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_LOGIN_IN + in_login + 1);
      MT_CHECK(strncmp(e->name, "Serverbound", 11) == 0);
      in_login++;
    } else if (id >= GRP_LOGIN_OUT && id < GRP_LOGIN_OUT + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_LOGIN_OUT + out_login + 1);
      MT_CHECK(strncmp(e->name, "Clientbound", 11) == 0);
      out_login++;
    } else if (id >= GRP_STATUS_IN && id < GRP_STATUS_IN + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_STATUS_IN + in_status + 1);
      MT_CHECK(strncmp(e->name, "Serverbound", 11) == 0);
      in_status++;
    } else if (id >= GRP_STATUS_OUT && id < GRP_STATUS_OUT + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_STATUS_OUT + out_status + 1);
      MT_CHECK(strncmp(e->name, "Clientbound", 11) == 0);
      out_status++;
    } else if (id >= GRP_HANDSHAKE && id < GRP_HANDSHAKE + 0x100) {
      MT_CHECK_EQ_U64(id, GRP_HANDSHAKE + handshake + 1);
      MT_CHECK(strcmp(e->name, "ClientIntentionPacket") == 0);
      handshake++;
    } else {
      MT_CHECK(!"id 越出已知分组");
    }
  }
  /* 1.20.1 协议态分布(与 packets.c 头注释一致;LC-2 内嵌变体并入分组:
     PLAY_IN 46+4=50、PLAY_OUT 109+3=112) */
  MT_CHECK_EQ_U64(in_play, 50);
  MT_CHECK_EQ_U64(out_play, 112);
  MT_CHECK_EQ_U64(in_login, 3);
  MT_CHECK_EQ_U64(out_login, 5);
  MT_CHECK_EQ_U64(in_status, 2);
  MT_CHECK_EQ_U64(out_status, 2);
  MT_CHECK_EQ_U64(handshake, 1);
}

/* ---- 访问器:逐项与目录同序、越界 null、负 index null ---- */
static void test_accessor(void) {
  for (u32 i = 0; i < mosaic_packets_catalog_count; i++)
    MT_CHECK(strcmp(mosaic_packet_catalog_name(i),
                    mosaic_packets_catalog[i].name) == 0);
  MT_CHECK(mosaic_packet_catalog_name(mosaic_packets_catalog_count) == NULL);
  MT_CHECK(mosaic_packet_catalog_name(0xFFFFFFFFu) == NULL);
}

int main(void) {
  MT_RUN(test_catalog_integrity);
  MT_RUN(test_group_ids);
  MT_RUN(test_accessor);
  if (!MT_RESULT()) return 1;
  printf("test_packets: all ok (%u entries)\n",
         (unsigned)mosaic_packets_catalog_count);
  return 0;
}
