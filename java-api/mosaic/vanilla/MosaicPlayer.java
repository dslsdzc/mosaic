package mosaic.vanilla;

/** 玩家会话。 */
public interface MosaicPlayer {
    MosaicEntityId entityId();
    String name();
    int gameMode();   /* 0=生存 1=创造 2=冒险 3=旁观(两代同序) */
    boolean online();
}
