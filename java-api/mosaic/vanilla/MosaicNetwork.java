package mosaic.vanilla;

import mosaic.Since;

/** 网络:包收发/监听器。 */
public interface MosaicNetwork {
    void sendPacket(int playerId, byte[] packetData);
    MosaicPacketListener listener();

    /** 包投影:原版包对象 → MosaicPacket 稳定投影(7.1)。typeId = 包目录 id
     *  (26.2 mojmap 类简单名直接对目录名;1.8.9 MCP 名经语义对照表;未命中 →
     *  0 UNKNOWN);direction 由包类名约定推导;playerId/sizeHint 恒 0(v1 无
     *  包内容序列化,见 README 已知边界)。null → 全零投影(不抛)。 */
    @Since(1)
    MosaicPacket packetOf(Object vanillaPacket);
}
