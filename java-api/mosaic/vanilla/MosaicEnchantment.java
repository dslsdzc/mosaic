package mosaic.vanilla;

/** 附魔域(26.2 world.item.enchantment.Enchantment ↔ 1.8.9 enchantment.Enchantment)。 */
public interface MosaicEnchantment {
    /** 注册表名。 */
    String registryName();
    /** 最大等级。 */
    int maxLevel();
    /** 附魔描述名(本地化键,如 "enchantment.damage.all")。 */
    String descriptionKey();
}
