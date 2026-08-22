package mosaic.vanilla;

/** 网络:包收发/监听器。 */
public interface MosaicNetwork {
    void sendPacket(int playerId, byte[] packetData);
    MosaicPacketListener listener();
}
