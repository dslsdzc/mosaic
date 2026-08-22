package mosaic.vanilla;

/** 世界:稳定句柄(26.2 world.level.Level ↔ 1.8.9 World 均转换为此)。
 *  注意:任务简报文件清单列名 MosaicWorld.java 但代码块缺失(简报漏项),
 *  此为最小补桩,仅含两代共同语义能力;简报原文补全时应以原文替换。 */
public interface MosaicWorld {
    /** 维度名(如 "minecraft:overworld");1.8.9 无维度概念,由 Provider 合成。 */
    String dimension();
    /** 方块状态查询;坐标越界或区块未加载返回 null。 */
    MosaicBlockState getBlock(MosaicBlockPos pos);
}
