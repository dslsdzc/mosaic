package mosaic.vanilla;

/** 方块实体(26.2 world.level.block.entity.BlockEntity ↔ 1.8.9 tileentity.TileEntity)。 */
public interface MosaicBlockEntity {
    /** 方块实体类型注册名(26.2 BuiltInRegistries.BLOCK_ENTITY_TYPE.getKey ↔
     *  1.8.9 classToNameMap 类名映射小写合成 "minecraft:chest")。 */
    String typeRegistryName();
    /** 所在坐标。 */
    MosaicBlockPos pos();
}
