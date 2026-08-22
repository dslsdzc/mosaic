package mosaic.vanilla;

import mosaic.Since;

/** 世界:稳定句柄(26.2 world.level.Level ↔ 1.8.9 world.World 均转换为此)。
 *  语义锚定(规格 §5):维度/方块读/实体遍历/世界推进。 */
public interface MosaicWorld {
    /** 维度资源名(如 "minecraft:overworld");26.2 直接映射,
     *  1.8.9 由 WorldProvider.dimensionId/dimensionName 合成。 */
    String dimension();
    /** 方块状态查询;坐标越界或区块未加载返回 null
     *  (1.8.9 World.getBlockState ↔ 26.2 Level.getBlockState)。 */
    MosaicBlockState getBlock(MosaicBlockPos pos);
    /** 已加载实体枚举(快照;1.8.9 loadedEntityList ↔ 26.2 getEntities().getAll())。 */
    MosaicEntity[] entities();
    /** 按实体 id 查询;不存在返回 null(1.8.9 World.getEntityByID ↔ 26.2 Level.getEntity)。 */
    MosaicEntity entityById(int entityId);
    /** 推进世界一个 tick(1.8.9 World.tick ↔ 26.2 Level.tick;签名差异 Provider 吸收)。 */
    void tick();
    /** 保存世界(1.8.9 saveAllWorlds ↔ 26.2 ServerLevel.save)。 */
    void save();
    /** 世界总游戏时间(1.8.9 getTotalWorldTime ↔ 26.2 getGameTime)。 */
    long gameTime();
    /** 放置方块(写路径;26.2 Level.setBlock ↔ 1.8.9 World.setBlockState)。返回是否成功。
     *  句柄未持有真实 Level(契约环境/失效句柄)时抛 MosaicHandleException。 */
    @Since(1)
    boolean setBlock(MosaicBlockPos pos, MosaicBlockState state);
}
