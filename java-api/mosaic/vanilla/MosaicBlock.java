package mosaic.vanilla;

import mosaic.Since;

/** 方块:稳定句柄(26.2 world.level.block.Block ↔ 1.8.9 block.Block 均转换为此)。 */
public interface MosaicBlock {
    /** 当前状态。 */
    MosaicBlockState state();
    /** 注册表名(如 "minecraft:stone");1.8.9 由数字 id 合成。 */
    String registryName();
}
