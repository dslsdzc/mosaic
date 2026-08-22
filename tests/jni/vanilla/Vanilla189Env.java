import mosaic.vanilla.*;
import mosaic.vanilla.internal.ReflectUtil;
import mosaic.vanilla.internal.Vanilla189Provider;

/** 1.8.9 环境:MCP 名反射构造。
 *  - 注册引导:26.2 为 Bootstrap.bootStrap(),1.8.9 为 Bootstrap.register()(init 包);
 *    Blocks/Items 静态字段初始化校验 Bootstrap.isRegistered(),必须先 register;
 *    register() → Block.registerBlocks() + Item.registerItems() + StatList.init() 等;
 *  - Block/Item:Blocks.stone / Items.diamond(1.8.9 为 Block/Item 直接实例);
 *  - NBT:NBTTagCompound 无参构造;
 *  - Registry:Block.blockRegistry(RegistryNamespacedDefaultedByKey,默认值 air)。
 *    不用 Item.itemRegistry:逆向核实 1.8.9 itemRegistry 无 "air" 条目,而契约测试
 *    "default-registered air resolves" 要求 air 可解析——blockRegistry 含 air(id 0)
 *    与 stone(id 1),id↔名双向,是契约所需的 1.8.9 注册表形态;
 *  - World:1.8.9 存在静态 MinecraftServer.getServer(),但契约环境无运行中服务端
 *    → 返回 null → 回退维度令牌 Integer(0)(dimensionId 0 = Overworld)。
 *    26.2 以 Level.OVERWORLD 令牌、1.8.9 以 dimensionId 令牌,Provider 均合成
 *    "minecraft:overworld",双代同值;真实 World 路径在运行中服务端照常生效。 */
public class Vanilla189Env implements VanillaEnv {
    public Vanilla189Env() throws Exception {
        // 注册入口:Bootstrap.register()(Block.registerBlocks + Item.registerItems ...)
        Class.forName("net.minecraft.init.Bootstrap").getMethod("register").invoke(null);
    }

    public MosaicProvider provider() {
        Vanilla189Provider p = new Vanilla189Provider();
        MosaicProviderRegistry.register(p);
        return p;
    }
    public Object worldObject() throws Exception {
        Object server = ReflectUtil.callStatic("net.minecraft.server.MinecraftServer", "getServer");
        if (server != null) {
            Object world = ReflectUtil.call(server, "worldServerForDimension", 0);
            if (world != null) return world;
        }
        return Integer.valueOf(0);   // 维度令牌:dimensionId 0 = Overworld(契约环境无运行中服务端)
    }
    public Object blockObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.init.Blocks", "stone");
    }
    public Object itemObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.init.Items", "diamond");
    }
    public Object nbtObject() throws Exception {
        return Class.forName("net.minecraft.nbt.NBTTagCompound").getDeclaredConstructor().newInstance();
    }
    public Object registryObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.block.Block", "blockRegistry");
    }
    public Object inventoryObject() throws Exception {
        // InventoryBasic(String, boolean, int):无 World 依赖(空槽 null);
        // 构造签名核实 mcp918/src/minecraft/net/minecraft/inventory/InventoryBasic.java:19
        return ReflectUtil.callConstructor("net.minecraft.inventory.InventoryBasic", "test", false, 5);
    }
    public Object commandObject() throws Exception {
        // 1.8.9 命令对象:net.minecraft.command.CommandHandler 隐式无参构造(逆向核实
        // mcp918/src/minecraft/net/minecraft/command/CommandHandler.java:类无声明构造器,
        // 字段内联初始化 commandMap/commandSet,无 server 依赖)。
        // ServerCommandManager 需 MinecraftServer 不可构造——契约只用 CommandHandler。
        return ReflectUtil.callConstructor("net.minecraft.command.CommandHandler");
    }
    public Object enchantmentObject() throws Exception {
        // 1.8.9 附魔:静态实例(Enchantment.sharpness = EnchantmentDamage(16,
        // "sharpness", 10, 0),Enchantment.java:54)。类加载即注册入
        // locationEnchantments 静态表(注册名逆查源);构造新实例需 (int id,
        // ResourceLocation, int, EnumEnchantmentType) 且重复 id 抛
        // IllegalArgumentException(Enchantment.java:100-110)——静态实例是
        // 契约环境唯一的真实路径(任务预设 "(Rarity,int,int)" 为 1.6-1.7 形态,
        // 1.8.9 逆向核实非该签名)。
        return ReflectUtil.fieldStatic("net.minecraft.enchantment.Enchantment", "sharpness");
    }
    public Object statusEffectObject() throws Exception {
        // 1.8.9 PotionEffect(int id, int duration, int amplifier) 轻参构造(PotionEffect
        // .java:36-39,仅存字段);id 10 = Potion.regeneration(ResourceLocation
        // ("regeneration"),Potion.java:37)——与 26.2 MobEffects.REGENERATION 对齐。
        // registryName 链(Potion.potionTypes/field_180150_I)在 Potion 类加载时填充,
        // 与 Bootstrap.register() 无依赖。
        return ReflectUtil.callConstructor("net.minecraft.potion.PotionEffect", 10, 100, 1);
    }
    public Object tagObject() throws Exception {
        // 1.8.9 无标签系统:逆向核实 uber jar 无 net/minecraft/tags 包(无 Tag/BlockTags
        // 类;标签概念 1.13+ 随数据驱动注册表引入)→ 契约环境无标签对象,真实路径断言
        // 跳过(if-available 守卫打印 NOTE;双代不对称:26.2 TagKey 可构造)。
        throw new ClassNotFoundException("1.8.9 has no net.minecraft.tags package (tag system is 1.13+)");
    }
}
