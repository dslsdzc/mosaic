package mosaic.vanilla;

public interface MosaicPlayerSession {
    /** 当前在线玩家 id 列表。 */
    int[] onlinePlayerIds();
    /** 按 id 取玩家;离线返回 null。 */
    MosaicPlayer byId(int playerId);
}
