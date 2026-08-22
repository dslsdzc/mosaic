package mosaic.vanilla;

/** Provider:版本差异全吸收;句柄持有原版引用,接口方法读取时转换。 */
public interface MosaicProvider {
    String providerId();            /* "vanilla-26.2" / "vanilla-1.8.9" */
    String mcVersion();             /* "26.2" / "1.8.9" */
    boolean supportsApi(int min, int max);

    MosaicBlock blockOf(Object vanillaBlock);
    MosaicBlockState blockStateOf(Object vanillaBlockState);
    MosaicItem itemOf(Object vanillaItem);
    MosaicItemStack itemStackOf(Object vanillaItemStack);
    MosaicWorld worldOf(Object vanillaWorld);
    MosaicEntity entityOf(Object vanillaEntity);
    MosaicPlayer playerOf(Object vanillaPlayer);
    MosaicInventory inventoryOf(Object vanillaInventory);
    MosaicRegistry registryOf(Object vanillaRegistry);
    MosaicNbt nbtOf(Object vanillaNbt);
}
