package mosaic.vanilla;

/** 配方域(26.2 world.item.crafting.Recipe ↔ 1.8.9 item.crafting.IRecipe)。 */
public interface MosaicRecipe {
    /** 注册表名(如 "minecraft:stone_from_cobblestone");1.8.9 由 Provider 合成/映射。 */
    String registryName();
    /** 输出物品栈。 */
    MosaicItemStack result();
    /** 配方类型名(26.2 RecipeType key ↔ 1.8.9 RecipeCategory 映射,Provider 吸收)。 */
    String type();
}
