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

        // Inventory(真实路径:两代均可构造无 World 依赖容器——26.2 SimpleContainer / 1.8.9 InventoryBasic)
        Object invObj = env.inventoryObject();
        MosaicInventory inv = p.inventoryOf(invObj);
        check(inv != null, "inventory handle");
        int slotCount = inv.slotCount();
        check(slotCount >= 0, "slotCount >= 0 (got " + slotCount + ")");
        check(inv.size() == slotCount, "size == slotCount (size " + inv.size() + ", slotCount " + slotCount + ")");
        // 空槽表示:26.2 为 ItemStack.EMPTY(Provider 转 null)、1.8.9 为 null —— 断言宽松但真实
        MosaicItemStack emptySlot = inv.getItem(0);
        check(emptySlot == null || emptySlot.count() == 0,
                "empty slot getItem(0) null or empty stack (got "
                        + (emptySlot == null ? "null" : "count " + emptySlot.count()) + ")");
        check(inv.slot(0).isEmpty(), "slot(0).isEmpty() on empty slot");

        // NBT(两代语义最稳定)
        Object nbtObj = env.nbtObject();
        MosaicNbt nbt = p.nbtOf(nbtObj);
        MosaicNbtCompound c = nbt.compound();
        check(c != null, "nbt compound");
        c.putString("k", "v");
        check(c.getString("k").equals("v"), "nbt roundtrip");
        c.putInt("i", 42);
        check(c.getInt("i") == 42, "nbt int roundtrip");

        // Registry:id↔名(M6-D:恒真式断言行已删——id>=0||id==-1、name!=null||
        // id==-1 对一切输入恒真,真断言在下方)
        Object regObj = env.registryObject();
        MosaicRegistry reg = p.registryOf(regObj);
        int id = reg.id("minecraft:stone");
        // 接口契约:未注册名 → -1,未注册 id → null(26.2 DefaultedRegistry 需存在性守卫)
        check(reg.id("minecraft:not_a_block") == -1,
                "unregistered name -> id -1 (got " + reg.id("minecraft:not_a_block") + ")");
        check(reg.name(-1) == null,
                "unregistered id -> name null (got '" + reg.name(-1) + "')");
        // 合法默认值注册名(id(air 的 id))仍须可解析
        int airId = reg.id("minecraft:air");
        check(airId >= 0 && "minecraft:air".equals(reg.name(airId)),
                "default-registered air resolves (" + airId + " -> " + reg.name(airId) + ")");

        // NBT:缺失键 getString → 空串(接口契约,26.2 Optional.empty 解包)
        check(c.getString("missing").equals(""),
                "nbt missing key -> empty string (got '" + c.getString("missing") + "')");

        // World 令牌默认契约
        String dim = world.dimension();
        check(dim != null && !dim.isEmpty(), "world dimension non-empty (got '" + dim + "')");
        check(world.entities().length == 0, "world entities empty");
        check(world.gameTime() == 0, "world gameTime 0 (got " + world.gameTime() + ")");

        // Entity/Player(环境限制):两者需真实 Level 才能构造,契约环境不可构造——
        // null 语义断言 + 真实路径待服务端环境(延续 world 的 null-safe 句柄先例)。
        // 两代 Provider 对 entityOf(null)/playerOf(null) 均返回 null-safe 句柄(非 null),
        // 句柄对 null 原版引用的字段默认值:type registryName "unknown"、name ""、
        // gameMode -1、online false(经读 Vanilla262Provider/Vanilla189Provider 实现核实,双代一致)。
        MosaicEntity e = p.entityOf(null);
        check(e != null, "entityOf(null) null-safe handle");
        MosaicEntityType et = e.type();
        check(et != null && "unknown".equals(et.registryName()),
                "entityOf(null) type registryName 'unknown' (got '"
                        + (et == null ? "null" : et.registryName()) + "')");
        MosaicPlayer player = p.playerOf(null);
        check(player != null, "playerOf(null) null-safe handle");
        check("".equals(player.name()), "playerOf(null) name '' (got '" + player.name() + "')");
        check(player.gameMode() == -1, "playerOf(null) gameMode -1 (got " + player.gameMode() + ")");
        check(!player.online(), "playerOf(null) online false");

        if (failures == 0) System.out.println("VANILLA CONTRACT PASSED (" + p.mcVersion() + ")");
        System.exit(failures == 0 ? 0 : 1);
    }
}
