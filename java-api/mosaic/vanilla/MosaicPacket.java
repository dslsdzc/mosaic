package mosaic.vanilla;

import mosaic.Since;

/** 网络包稳定投影(网络域,Task 7):语义锚定包目录 id(include/mosaic/packets.h,
 *  168 目录,1.20.1 锚定)。typeId = 包目录 id(未命中目录 → 0 = UNKNOWN);
 *  direction 与包目录 id 分组方向一致(0x01xx/0x05xx/0x07xx/0x09xx = IN,
 *  0x02xx/0x06xx/0x08xx = OUT;未知包按包类名约定推导);sizeHint = 包内容
 *  序列化大小——v1 无包内容序列化,恒 0(已知边界,README 标注);playerId =
 *  连接玩家(packetOf 投影无连接上下文 → 0,真实分发路径在服务端环境填充)。 */
@Since(1)
public interface MosaicPacket {
    /** 连接玩家 id(非游戏阶段连接(登录/状态/握手)→ 0;投影无连接上下文 → 0)。 */
    int playerId();
    /** 方向:IN = 玩家 → 服务端(Serverbound / 1.8.9 C 包),OUT = 服务端 → 玩家
     *  (Clientbound / 1.8.9 S 包)。 */
    Direction direction();
    /** 包目录 id(include/mosaic/packets.h);未命中目录 → 0(UNKNOWN)。 */
    int typeId();
    /** 包内容序列化大小(字节);v1 无包内容序列化 → 恒 0。 */
    int sizeHint();

    /** 包方向:IN = 玩家 → 服务端,OUT = 服务端 → 玩家。 */
    @Since(1)
    enum Direction { IN, OUT }
}
