package mosaic.vanilla;

import mosaic.Since;

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

    /** 命令域句柄工厂(26.2 Brigadier Commands ↔ 1.8.9 CommandHandler)。 */
    @Since(1)
    MosaicCommand commandOf(Object vanillaCommand);
    /** 网络域句柄工厂(26.2 PacketListener/Connection ↔ 1.8.9 NetHandler)。 */
    @Since(1)
    MosaicNetwork networkOf(Object vanillaNetwork);
    /** 配方域句柄工厂(26.2 world.item.crafting.Recipe ↔ 1.8.9 item.crafting.IRecipe)。
     *  契约环境 Recipe 为接口不可轻量构造 → null 语义为主(与 Entity 先例一致),
     *  真实路径在服务端环境可用。 */
    @Since(1)
    MosaicRecipe recipeOf(Object vanillaRecipe);
    /** 附魔域句柄工厂(26.2 world.item.enchantment.Enchantment(记录)↔ 1.8.9
     *  enchantment.Enchantment)。26.2 契约环境可构造 Enchantment 记录(轻参)、
     *  1.8.9 静态实例可用 → 双代真实路径 + null 语义。 */
    @Since(1)
    MosaicEnchantment enchantmentOf(Object vanillaEnchantment);

    /** 活体实体句柄工厂(26.2 LivingEntity ↔ 1.8.9 EntityLivingBase)。契约环境不可
     *  构造(需真实实体对象,与 Entity 先例一致)→ null 语义为主,真实路径在服务端环境。 */
    @Since(1)
    MosaicLivingEntity livingEntityOf(Object vanillaLivingEntity);
    /** 状态效果句柄工厂(26.2 MobEffectInstance ↔ 1.8.9 PotionEffect)。双代契约环境
     *  均可轻参构造(26.2 MobEffectInstance(Holder, int, int) / 1.8.9 PotionEffect(int,
     *  int, int))→ 双代真实路径 + null 语义。 */
    @Since(1)
    MosaicStatusEffect statusEffectOf(Object vanillaEffectInstance);
    /** 标签句柄工厂(26.2 TagKey 记录轻参构造 ↔ 1.8.9 无标签系统(jar 无 net.minecraft.
     *  tags,逆向核实)→ 26.2 真实路径(TagKey registryName;contents 已绑定标签 →
     *  真实内容、未绑定 → 空)+ 1.8.9 恒空降级 + 双代 null 语义。 */
    @Since(1)
    MosaicTag tagOf(Object vanillaTag);
    /** 方块实体句柄工厂(26.2 BlockEntity ↔ 1.8.9 TileEntity)。契约环境不可轻参构造
     *  (26.2 构造器 public 但需 BlockEntityType+BlockState 装配、1.8.9 抽象类)→
     *  null 语义为主,真实路径在服务端环境。 */
    @Since(1)
    MosaicBlockEntity blockEntityOf(Object vanillaBlockEntity);
}
