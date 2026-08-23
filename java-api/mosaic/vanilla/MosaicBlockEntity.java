package mosaic.vanilla;

/** 方块实体(26.2 world.level.block.entity.BlockEntity ↔ 1.8.9 tileentity.TileEntity)。
 *  构造器可见性不对称:26.2 构造器 public(BlockEntity.java:51,需
 *  BlockEntityType+BlockState 装配)、1.8.9 非 public(抽象类)——双代契约环境
 *  均不可轻参构造,句柄经 Provider 工厂获取(null 语义为主)。 */
public interface MosaicBlockEntity {
    /** 方块实体类型注册名(26.2 BuiltInRegistries.BLOCK_ENTITY_TYPE.getKey ↔
     *  1.8.9 classToNameMap 类名映射小写合成 "minecraft:chest")。 */
    String typeRegistryName();
    /** 所在坐标。 */
    MosaicBlockPos pos();
}
