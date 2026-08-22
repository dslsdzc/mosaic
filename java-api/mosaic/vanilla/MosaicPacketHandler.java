package mosaic.vanilla;

@FunctionalInterface
public interface MosaicPacketHandler {
    void handle(int playerId, byte[] packetData);
}
