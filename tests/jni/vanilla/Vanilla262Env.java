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
}
