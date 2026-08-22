import mosaic.vanilla.*;
import mosaic.vanilla.internal.ReflectUtil;
import mosaic.vanilla.internal.Vanilla262Provider;

/** 26.2 环境:反射构造原版对象。
 *  - Block/Item:注册表静态字段(Blocks.STONE / Items.DIAMOND,26.2 为 Block/Item 直接实例);
 *  - NBT:CompoundTag 无参构造;
 *  - Registry:BuiltInRegistries.BLOCK(DefaultedRegistry<Block>);
 *  - World:26.2 无静态 MinecraftServer 实例/无静态 getServer(Bootstrap 只有 bootStrap),
 *    契约环境无运行中服务端无法构造真实 Level(Level 抽象、ServerLevel 需完整服务端)。
 *    返回 Level.OVERWORLD(ResourceKey<Level>)作维度令牌:worldOf 对非 Level 对象返回
 *    null-safe 句柄(dimension 取令牌资源名,其余默认值/空)——见 Task 6 报告限制说明。 */
public class Vanilla262Env implements VanillaEnv {
    public Vanilla262Env() throws Exception {
        // 服务端启动序列缺的两步,契约环境补齐:
        // 1) SharedConstants.setVersion(DetectedVersion.BUILT_IN)(服务端由 Main 从
        //    version.json 设置;DataFixers 类初始化需要,否则 "Game version not set");
        // 2) Bootstrap.bootStrap() 引导注册表(Blocks/Items 静态字段装载时写入 BuiltInRegistries)
        Class<?> wv = Class.forName("net.minecraft.WorldVersion");
        Class.forName("net.minecraft.SharedConstants")
            .getMethod("setVersion", wv)
            .invoke(null, Class.forName("net.minecraft.DetectedVersion").getField("BUILT_IN").get(null));
        Class.forName("net.minecraft.server.Bootstrap").getMethod("bootStrap").invoke(null);
    }

    public MosaicProvider provider() {
        Vanilla262Provider p = new Vanilla262Provider();
        MosaicProviderRegistry.register(p);
        return p;
    }
    public Object worldObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.world.level.Level", "OVERWORLD");
    }
    public Object blockObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.world.level.block.Blocks", "STONE");
    }
    public Object itemObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.world.item.Items", "DIAMOND");
    }
    public Object nbtObject() throws Exception {
        return Class.forName("net.minecraft.nbt.CompoundTag").getDeclaredConstructor().newInstance();
    }
    public Object registryObject() throws Exception {
        return ReflectUtil.fieldStatic("net.minecraft.core.registries.BuiltInRegistries", "BLOCK");
    }
    public Object inventoryObject() throws Exception {
        // SimpleContainer(int):无 World 依赖(空槽 ItemStack.EMPTY);
        // 构造签名核实 ~/minecraft26.2/decompiled/net/minecraft/world/SimpleContainer.java:18
        return ReflectUtil.callConstructor("net.minecraft.world.SimpleContainer", 5);
    }
    public Object commandObject() throws Exception {
        // 26.2 命令对象:com.mojang.brigadier.CommandDispatcher 无参构造(Brigadier 库在
        // Mojang 运行库内)。构造核实:brigadier-1.3.10.jar javap CommandDispatcher()。
        // 无服务器环境:register 仅操作本地树;execute 需 CommandSourceStack 不可用——
        // 契约只断言 register 后的 registered() 列表(见 VanillaContractTest 注释)。
        return ReflectUtil.callConstructor("com.mojang.brigadier.CommandDispatcher");
    }
    public Object enchantmentObject() throws Exception {
        // 26.2 Enchantment 为记录(非抽象类),契约环境可轻参构造(逆向核实
        // Enchantment.java:47 record Enchantment(Component, EnchantmentDefinition,
        // HolderSet, DataComponentMap)):
        // 1) description:Component.translatable("enchantment.damage.all")
        //    (Component.java:139)——与 1.8.9 Enchantment.sharpness.getName() 同键
        //    (1.8.9 实测 "enchantment.damage.all",protectionName[0],
        //    EnchantmentDamage.java:15;任务 doc 示例 "enchantment.damage.sharpness"
        //    为 1.9+ 命名,1.8.9 用 damage.all),契约可断言精确本地化键(双代同值);
        // 2) definition:Enchantment$EnchantmentDefinition 构造器(HolderSet, Optional,
        //    int, int, Cost, Cost, int, List)——supportedItems 用
        //    HolderSet.direct(List.of(Items.DIAMOND.builtInRegistryHolder()))
        //    (HolderSet.java:57 List 重载;varargs 重载 ReflectUtil 不可匹配);
        //    cost 用 Enchantment$Cost(base, perLevelAboveFirst) 构造器;
        // 3) exclusiveSet:HolderSet.empty()(HolderSet.java:48);effects:DataComponentMap.EMPTY。
        Object desc = ReflectUtil.callStatic("net.minecraft.network.chat.Component",
                "translatable", "enchantment.damage.all");
        Object item = ReflectUtil.fieldStatic("net.minecraft.world.item.Items", "DIAMOND");
        Object holder = ReflectUtil.call(item, "builtInRegistryHolder");
        Object supported = ReflectUtil.callStatic("net.minecraft.core.HolderSet",
                "direct", java.util.List.of(holder));
        Object cost = ReflectUtil.callConstructor(
                "net.minecraft.world.item.enchantment.Enchantment$Cost", 1, 0);
        Object def = ReflectUtil.callConstructor(
                "net.minecraft.world.item.enchantment.Enchantment$EnchantmentDefinition",
                supported, java.util.Optional.empty(), 5, 3, cost, cost, 1,
                java.util.List.of(ReflectUtil.fieldStatic(
                        "net.minecraft.world.entity.EquipmentSlotGroup", "MAINHAND")));
        Object exclusive = ReflectUtil.callStatic("net.minecraft.core.HolderSet", "empty");
        Object effects = ReflectUtil.fieldStatic("net.minecraft.core.component.DataComponentMap", "EMPTY");
        return ReflectUtil.callConstructor("net.minecraft.world.item.enchantment.Enchantment",
                desc, def, exclusive, effects);
    }
    public Object statusEffectObject() throws Exception {
        // 26.2 MobEffectInstance 为类(非记录;MobEffectInstance.java:25),3 参构造器
        // MobEffectInstance(Holder<MobEffect>, int, int)(MobEffectInstance.java:56)
        // 轻参可构造:MobEffects.REGENERATION 为 Holder<MobEffect> 静态字段
        // (MobEffects.java:54,registerForHolder 注册入 BuiltInRegistries.MOB_EFFECT,
        // Bootstrap.bootStrap 后为已绑定 Holder)。值断言 duration 100 / amplifier 1,
        // 与 1.8.9 PotionEffect(10, 100, 1) 对齐(id 10 = regeneration,Potion.java:37)。
        Object holder = ReflectUtil.fieldStatic("net.minecraft.world.effect.MobEffects", "REGENERATION");
        return ReflectUtil.callConstructor("net.minecraft.world.effect.MobEffectInstance", holder, 100, 1);
    }
    public Object tagObject() throws Exception {
        // 26.2 TagKey 为记录(record TagKey(ResourceKey registry, Identifier location),
        // TagKey.java:14),create(ResourceKey, Identifier) 轻参构造(TagKey.java:37):
        // Registries.BLOCK(ResourceKey<Registry<Block>>,Registries.java:156)+
        // Identifier.parse("planks")。
        // 注意:标签内容为数据驱动(标签 JSON 经 TagLoader 世界加载期绑定),契约环境
        // BuiltInRegistries 无已绑定标签 → contents() 空数组;真实内容查询在服务端环境。
        Object regKey = ReflectUtil.fieldStatic("net.minecraft.core.registries.Registries", "BLOCK");
        Object ident = ReflectUtil.callStatic("net.minecraft.resources.Identifier", "parse", "planks");
        return ReflectUtil.callStatic("net.minecraft.tags.TagKey", "create", regKey, ident);
    }
}
