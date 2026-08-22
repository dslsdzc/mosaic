package mosaic.vanilla;

import mosaic.Since;

/** 物品:稳定句柄。 */
public interface MosaicItem {
    String registryName();
    int maxStackSize();
    /** 组件集(26.2 起;1.8.9 为空实现)。 */
    @Since(1)
    MosaicComponents components();
}
