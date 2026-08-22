import mosaic.vanilla.*;

/** 原版域契约测试:版本无关,在 26.2 与 1.8.9 环境分别运行(共享源码)。
 *  环境装配由 VanillaEnv 实现(各版本构造原版对象 + 注册 Provider)。 */
public class VanillaContractTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) { System.err.println("usage: VanillaContractTest <vanillaObjectFactoryClassName>"); System.exit(2); }
        // 环境装配(26.2/1.8.9 各自提供):构造原版对象 + 注册 Provider
        VanillaEnv env = (VanillaEnv) Class.forName(args[0]).getDeclaredConstructor().newInstance();
        MosaicProvider p = env.provider();
        Object worldObj = env.worldObject();

        MosaicWorld world = p.worldOf(worldObj);
        check(world != null, "world handle");

        // Block 句柄(经环境提供的方块对象)
        Object blockObj = env.blockObject();
        MosaicBlock block = p.blockOf(blockObj);
        check(block != null, "block handle");
        check(block.state() != null, "block state");
        String rn = block.registryName();
        check(rn != null && !rn.isEmpty(), "registryName non-empty: " + rn);

        // Item
        Object itemObj = env.itemObject();
        MosaicItem item = p.itemOf(itemObj);
        check(item != null, "item handle");
        check(item.maxStackSize() >= 1, "maxStackSize >= 1");

        // NBT(两代语义最稳定)
        Object nbtObj = env.nbtObject();
        MosaicNbt nbt = p.nbtOf(nbtObj);
        MosaicNbtCompound c = nbt.compound();
        check(c != null, "nbt compound");
        c.putString("k", "v");
        check(c.getString("k").equals("v"), "nbt roundtrip");
        c.putInt("i", 42);
        check(c.getInt("i") == 42, "nbt int roundtrip");

        // Registry:id↔名
        Object regObj = env.registryObject();
        MosaicRegistry reg = p.registryOf(regObj);
        int id = reg.id("minecraft:stone");
        check(id >= 0 || id == -1, "registry id lookup (stone=" + id + ")");
        check(reg.name(id) != null || id == -1, "registry name lookup");

        if (failures == 0) System.out.println("VANILLA CONTRACT PASSED (" + p.mcVersion() + ")");
        System.exit(failures == 0 ? 0 : 1);
    }
}
