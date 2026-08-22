package mosaic.vanilla;

import mosaic.Since;

/** 注册表:id↔名双向映射(数字 id/注册表 id 差异全在 Provider)。 */
public interface MosaicRegistry {
    /** 名 → id;未注册 -1。 */
    int id(String registryName);
    /** id → 名;未注册 null。 */
    String name(int id);

    /** 注册方块(扁平化语义:注册名 → 注册表;26.2 BuiltInRegistries 直注册,
     *  1.8.9 blockRegistry 映射数字 ID 适配)。重复注册抛 MosaicApiException。
     *  vanillaBlock:原版方块对象(来自服务端上下文)。 */
    @Since(1)
    MosaicRegistryEntry registerBlock(String registryName, Object vanillaBlock);
    /** 注册物品(同上;1.8.9 itemRegistry 适配)。 */
    @Since(1)
    MosaicRegistryEntry registerItem(String registryName, Object vanillaItem);
}
