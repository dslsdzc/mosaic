package mosaic.vanilla;

import mosaic.Since;

/** 物品:稳定句柄。 */
public interface MosaicItem {
    String registryName();
    int maxStackSize();
    /** 组件集(26.2 起;1.8.9 为空实现)。 */
    @Since(1)
    MosaicComponents components();
    /** 最大耐久(不可损坏物品为 0,vanilla getMaxDamage 语义:26.2 组件
     *  DataComponents.MAX_DAMAGE ↔ 1.8.9 getMaxDamage 字段)。 */
    @Since(1)
    int maxDamage();
    /** 是否可损坏(26.2:maxDamage > 0;1.8.9:isDamageable)。 */
    @Since(1)
    boolean damageable();
}
