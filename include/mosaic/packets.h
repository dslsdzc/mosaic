#ifndef MOSAIC_PACKETS_H
#define MOSAIC_PACKETS_H
#include "mosaic/base.h"

/* 包类型目录 v1(网络域,Task 6)。
   锚定 1.20.1 包集合(mojmap 名,net.minecraft.network.protocol.* 下全部
   Serverbound 与 Clientbound 包类 + 登录/状态/握手组,共 168 类),方向分组
   顺序分配 id:
     UNKNOWN=0、PLAY_IN 0x0101..、PLAY_OUT 0x0201..、CONFIG_IN 0x0301..
     (1.20.1 无 config 协议态,组空)、CONFIG_OUT 0x0401..(组空)、
     LOGIN_IN 0x0501..、LOGIN_OUT 0x0601..、STATUS_IN 0x0701..、
     STATUS_OUT 0x0801..、HANDSHAKE_IN 0x0901..
   (1.20.1 握手态唯一包 = ClientIntentionPacket;brief 8 组未列握手组,
   追加 0x09xx 不占既有基址)。组内顺序 = 目录序(按名字升序)。
   26.2(mojmap)/1.8.9(MCP)经 Provider 语义映射进同一 id 空间(方向相同、
   语义相近的包对到同一 id;无对应 → UNKNOWN)。
   BundleDelimiterPacket/BundlePacket 为编码器内非方向性工具类(抽象基类/
   特殊标记,不单独经连接收发包),不入目录——出现即 UNKNOWN(0)。
   命名 = mojmap 类简单名(如 "ClientboundChatPacket"——1.20.1 实为
   ClientboundSystemChatPacket;清单由 ci/gen_packet_map.sh 从 Mojang
   server_mappings 提取,src/packets.c 与 server.txt 逐项配对校验)。 */

typedef struct { const char *name; u32 id; } mosaic_packet_entry;

/* 包目录:按名字升序(与 events.c 同一纪律)。实现见 src/packets.c */
extern const mosaic_packet_entry mosaic_packets_catalog[];
extern const u32 mosaic_packets_catalog_count;

/* 目录访问器(N2 包目录双端一致门禁用):返回第 index 个包名(目录内静态
   字符串,与 mosaic_packets_catalog 同序);越界 → NULL。Java 契约测试经
   JNI 遍历全部名字与 PACKET_NAMES 常量表逐项比对——packets.c 增删目录名
   → Java 比对失败 → 测试红,防目录跨语言漂移。 */
const char *mosaic_packet_catalog_name(u32 index);

#endif
