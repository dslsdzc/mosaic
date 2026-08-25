#ifndef MOSAIC_PACKETS_H
#define MOSAIC_PACKETS_H
#include "mosaic/base.h"

/* 包类型目录 v1(网络域,Task 6)。
   锚定 1.20.1 包集合(mojmap 名,net.minecraft.network.protocol.* 下全部
   Serverbound 与 Clientbound 包类 + 登录/状态/握手组 + 内嵌包变体,
   共 175 类 = 168 顶层 + 7 变体),方向分组顺序分配 id:
     UNKNOWN=0、PLAY_IN 0x0101..、PLAY_OUT 0x0201..、CONFIG_IN 0x0301..
     (1.20.1 无 config 协议态,组空)、CONFIG_OUT 0x0401..(组空)、
     LOGIN_IN 0x0501..、LOGIN_OUT 0x0601..、STATUS_IN 0x0701..、
     STATUS_OUT 0x0801..、HANDSHAKE_IN 0x0901..
   (1.20.1 握手态唯一包 = ClientIntentionPacket;brief 8 组未列握手组,
   追加 0x09xx 不占既有基址)。组内顺序 = 目录序(原始 168 条按名字升序)。
   26.2(mojmap)/1.8.9(MCP)经 Provider 语义映射进同一 id 空间(方向相同、
   语义相近的包对到同一 id;无对应 → UNKNOWN)。
   [LC-2] 内嵌包变体:ServerboundMovePlayerPacket(zx)与
   ClientboundMoveEntityPacket(wl)为抽象包类,运行时收发包实体 = 内嵌
   子类($Pos/$PosRot/$Rot/$StatusOnly、$Pos/$PosRot/$Rot,混淆名 zx$a-d /
   wl$a-c)。目录以 mojmap 内嵌名(带 '$',如 "ServerboundMovePlayerPacket
   $Pos")追加在目录尾部(追加块内按名升序;变体名 strcmp 序位在原 168 条
   之前,故追加块破坏全局升序——全局升序纪律仅适用于原 168 条,追加块
   独立升序,packets.c 注释注明);id = 组基 + 组内序(既有 id 不动,新 id
   按组续号:PLAY_IN 0x012F..0x0132、PLAY_OUT 0x026E..0x0270)。其余包类
   的内嵌类(枚举/接口/记录/数据持有类)非 Packet 子类,不入目录。
   BundleDelimiterPacket/BundlePacket 为编码器内非方向性工具类(抽象基类/
   特殊标记,不单独经连接收发包),不入目录——出现即 UNKNOWN(0)。
   命名 = mojmap 类简单名(如 "ClientboundChatPacket"——1.20.1 实为
   ClientboundSystemChatPacket;清单由 ci/gen_packet_map.sh 从 Mojang
   server_mappings 提取,src/packets.c 与 server.txt 逐项配对校验)。
   运行时查表键 = 混淆类全名(Class.getName()):内嵌类返回 a$b 形式(如
   "zx$a"),与 gen_packet_map.sh 生成的 PacketMap.java 键一致。 */

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
