package mosaic.vanilla;

public interface MosaicPacketListener {
    /** 注册包处理(按包类型名);返回订阅可关闭。 */
    AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler);
}
