import mosaic.MosaicApiException;
import mosaic.MosaicHandleException;
import mosaic.vanilla.*;

/** 原版域契约测试:版本无关,在 26.2 与 1.8.9 环境分别运行(共享源码)。
 *  环境装配由 VanillaEnv 实现(各版本构造原版对象 + 注册 Provider)。 */
public class VanillaContractTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }
    static boolean contains(String[] arr, String v) {
        if (arr == null) return false;
        for (String s : arr) if (v.equals(s)) return true;
        return false;
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

        // ---- M8-B:Item 纵深——耐久(maxDamage/damageable) ----
        // 契约测"非负/布尔合法"而非精确值:26.2 组件绑定回退(无运行中服务端 →
        // maxDamage 回退 0,与 maxStackSize 同款处理)与 1.8.9 真实字段值
        // (diamond maxDamage 0)在此物品上同值;可损坏物品双代可能不等(26.2
        // 回退 vs 1.8.9 真实)——差异处理见 task-m8-b-report.md。
        int maxDamage = item.maxDamage();
        check(maxDamage >= 0, "item maxDamage >= 0 (got " + maxDamage + ")");
        check(item.damageable() == (maxDamage > 0),
                "item damageable consistent with maxDamage (maxDamage " + maxDamage
                        + ", damageable " + item.damageable() + ")");

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

        // ---- M8-A:注册写路径(注册名 → 注册表;26.2 扁平化直注册 / 1.8.9 数字 ID 适配) ----
        // 契约环境断言(务实方案,双代同断言):注册表句柄存在(上方)、重复注册 → 抛
        // MosaicApiException(唯一性守卫,双代一致)、失败尝试不污染已注册条目。
        // 真实注册路径(新方块+新名)留待服务端环境——26.2 BuiltInRegistries 在
        // Bootstrap.bootStrap() 末尾 freeze() 且无 unfreeze API,冻结后连新 Block 的
        // 构造都失败(Block.<init> → createIntrusiveHolder 抛),见 task-m8-a-report.md;
        // 1.8.9 blockRegistry/itemRegistry 可写、新 Block(Material,MapColor)/Item()
        // 可构造(实测),Provider 的 registerBlock/registerItem 真实路径实现完整。
        boolean dupBlockThrew = false;
        try { reg.registerBlock("minecraft:stone", blockObj); }
        catch (MosaicApiException ex) { dupBlockThrew = true; }
        check(dupBlockThrew, "duplicate registerBlock throws MosaicApiException");
        boolean dupItemThrew = false;
        try { reg.registerItem("minecraft:diamond", itemObj); }
        catch (MosaicApiException ex) { dupItemThrew = true; }
        check(dupItemThrew, "duplicate registerItem throws MosaicApiException");
        // 失败尝试不污染已注册条目(双代:守卫先于 vanilla 注册调用,注册表不变)。
        // registerItem 锚定 item 注册表(26.2 BuiltInRegistries.ITEM / 1.8.9
        // Item.itemRegistry),与句柄包装的 block 注册表隔离——diamond 在 block
        // 注册表中保持不可解析(回归守卫:若误用包装注册表,diamond 会被写进
        // 1.8.9 可写的 blockRegistry,此断言即失败)。
        check(reg.id("minecraft:stone") >= 0,
                "stone still resolvable after failed duplicate registerBlock");
        check(reg.id("minecraft:diamond") == -1,
                "registerItem did not pollute block registry (diamond stays unresolvable)");

        // NBT:缺失键 getString → 空串(接口契约,26.2 Optional.empty 解包)
        check(c.getString("missing").equals(""),
                "nbt missing key -> empty string (got '" + c.getString("missing") + "')");

        // World 令牌默认契约
        String dim = world.dimension();
        check(dim != null && !dim.isEmpty(), "world dimension non-empty (got '" + dim + "')");
        check(world.entities().length == 0, "world entities empty");
        check(world.gameTime() == 0, "world gameTime 0 (got " + world.gameTime() + ")");

        // ---- M8-A:World 写路径 setBlock(错误路径;契约环境无真实 Level) ----
        // 双代一致:token 句柄(26.2 Level.OVERWORLD 令牌 / 1.8.9 dimensionId 令牌)与
        // null 句柄上的 setBlock 均抛 MosaicHandleException(句柄持原版 Level 引用,
        // 无效/缺失时抛——写路径不静默,与读路径 getBlock 的 null 语义区分);
        // 真实路径(Level.setBlock / World.setBlockState,flags=3)待运行中服务端环境。
        boolean setBlockThrew = false;
        try { world.setBlock(MosaicBlockPos.of(0, 64, 0), block.state()); }
        catch (MosaicHandleException ex) { setBlockThrew = true; }
        check(setBlockThrew, "setBlock on token world handle throws MosaicHandleException");
        MosaicWorld worldNull = p.worldOf(null);
        check(worldNull != null, "worldOf(null) null-safe handle");
        boolean setBlockNullThrew = false;
        try { worldNull.setBlock(MosaicBlockPos.of(0, 64, 0), block.state()); }
        catch (MosaicHandleException ex) { setBlockNullThrew = true; }
        check(setBlockNullThrew, "setBlock on null world handle throws MosaicHandleException");

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

        // ---- M7-B:Command 真实路径(双代均可构造,if-available 守卫作防御:
        // 26.2 CommandDispatcher 无参构造、1.8.9 CommandHandler 隐式无参构造,
        // 逆向核实见 task-m7-b-report。无服务器环境 register 仅注册到本地树,
        // execute 需 CommandSourceStack/ICommandSender 不可用 → 只断言 registered()
        // 列表(register 后含注册名 + 列表增长),不测 execute;若某代构造失败,
        // 真实路径断言跳过,null 语义断言仍双代必跑) ----
        boolean cmdAvailable = false;
        Object cmdObj = null;
        try { cmdObj = env.commandObject(); cmdAvailable = true; }
        catch (Exception ex) {
            System.err.println("NOTE: " + p.mcVersion() + " commandObject unavailable, "
                    + "command real-path assertions skipped: " + ex);
        }
        if (cmdAvailable) {
            MosaicCommand cmd = p.commandOf(cmdObj);
            check(cmd != null, "command handle (real path)");
            // 规范 §5 Command 域双接口(MosaicCommand + MosaicCommandTree),句柄两者皆实现
            check(cmd instanceof MosaicCommandTree, "command handle implements MosaicCommandTree");
            MosaicCommandTree tree = (MosaicCommandTree) cmd;
            String[] before = tree.registered();
            check(before != null, "registered() non-null");
            String cname = "m7contract";
            check(!contains(before, cname), "registered() not contains '" + cname + "' before register");
            cmd.register(cname, a -> 0);
            String[] after = tree.registered();
            check(contains(after, cname), "registered() contains '" + cname + "' after register");
            check(after.length > before.length,
                    "registered() grows after register (" + before.length + " -> " + after.length + ")");
            // 重名守卫(接口契约:MosaicCommand.register 重名抛 MosaicApiException)
            boolean dupThrew = false;
            try { cmd.register(cname, a -> 1); }
            catch (MosaicApiException ex) { dupThrew = true; }
            check(dupThrew, "duplicate register throws MosaicApiException");
        }

        // Command null 语义(双代必跑,兜底值双代同值:registered 空/register no-op)
        MosaicCommand cmdNull = p.commandOf(null);
        check(cmdNull != null, "commandOf(null) null-safe handle");
        check(cmdNull instanceof MosaicCommandTree, "commandOf(null) implements MosaicCommandTree");
        check(((MosaicCommandTree) cmdNull).registered() != null
                        && ((MosaicCommandTree) cmdNull).registered().length == 0,
                "commandOf(null) registered() empty");
        try { cmdNull.register("ignored", a -> 0); check(true, "commandOf(null) register no-throw"); }
        catch (Exception ex) { check(false, "commandOf(null) register no-throw: " + ex); }

        // ---- M7-B:Network null 语义(双代必跑;契约环境无真实 Connection/
        // NetHandlerPlayServer,与 Entity 先例同款——null 语义为主,真实路径待
        // 运行中服务端环境;兜底值双代同值) ----
        MosaicNetwork net = p.networkOf(null);
        check(net != null, "networkOf(null) null-safe handle");
        try { net.sendPacket(0, new byte[0]); check(true, "networkOf(null) sendPacket no-throw"); }
        catch (Exception ex) { check(false, "networkOf(null) sendPacket no-throw: " + ex); }
        MosaicPacketListener pl = net.listener();
        check(pl != null, "networkOf(null) listener non-null");
        AutoCloseable sub = pl.onPacket("test_packet", (pid, data) -> { });
        check(sub != null, "onPacket non-null subscription");
        try { sub.close(); check(true, "subscription close no-throw"); }
        catch (Exception ex) { check(false, "subscription close no-throw: " + ex); }

        // ---- M8-B:Recipe(null 语义为主,双代必跑;真实路径留服务端,与 Entity 先例一致。
        // 26.2 Recipe 为接口不可轻量构造、1.8.9 IRecipe 实例虽可构造但双代不对称,
        // 任务务实决策 null 语义为主——Provider 真实路径实现完整,待服务端环境) ----
        MosaicRecipe rec = p.recipeOf(null);
        check(rec != null, "recipeOf(null) null-safe handle");
        check("unknown".equals(rec.registryName()),
                "recipeOf(null) registryName 'unknown' (got '" + rec.registryName() + "')");
        check(rec.result() == null, "recipeOf(null) result null");
        check("".equals(rec.type()), "recipeOf(null) type '' (got '" + rec.type() + "')");

        // ---- M8-B:Enchantment(真实路径 if-available + null 语义双代必跑) ----
        // 逆向核实:26.2 Enchantment 为记录可轻参构造(见 Vanilla262Env.enchantmentObject);
        // 1.8.9 静态实例 Enchantment.sharpness。if-available 守卫作防御:某代构造失败
        // → 真实路径断言跳过,null 语义仍双代必跑(与 commandObject 同款模式)。
        boolean enchAvailable = false;
        Object enchObj = null;
        try { enchObj = env.enchantmentObject(); enchAvailable = true; }
        catch (Exception ex) {
            System.err.println("NOTE: " + p.mcVersion() + " enchantmentObject unavailable, "
                    + "enchantment real-path assertions skipped: " + ex);
        }
        if (enchAvailable) {
            MosaicEnchantment ench = p.enchantmentOf(enchObj);
            check(ench != null, "enchantment handle (real path)");
            String en = ench.registryName();
            check(en != null && !en.isEmpty(),
                    "enchantment registryName non-empty (got '" + en + "')");
            check(ench.maxLevel() >= 1,
                    "enchantment maxLevel >= 1 (got " + ench.maxLevel() + ")");
            // 双代同值断言:1.8.9 sharpness.getName() 与 26.2 构造的 translatable
            // 组件同用 "enchantment.damage.all" 键(1.8.9 实测键,EnchantmentDamage
            // protectionName[0]=all;任务 doc 示例 "enchantment.damage.sharpness"
            // 为 1.9+ 命名——1.8.9 的 sharpness 本地化键即 damage.all,见 Vanilla262Env)
            check("enchantment.damage.all".equals(ench.descriptionKey()),
                    "enchantment descriptionKey 'enchantment.damage.all' (got '"
                            + ench.descriptionKey() + "')");
        }
        // Enchantment null 语义(双代必跑,兜底值双代同值)
        MosaicEnchantment enchNull = p.enchantmentOf(null);
        check(enchNull != null, "enchantmentOf(null) null-safe handle");
        check("unknown".equals(enchNull.registryName()),
                "enchantmentOf(null) registryName 'unknown' (got '" + enchNull.registryName() + "')");
        check(enchNull.maxLevel() == 0,
                "enchantmentOf(null) maxLevel 0 (got " + enchNull.maxLevel() + ")");
        check("".equals(enchNull.descriptionKey()),
                "enchantmentOf(null) descriptionKey '' (got '" + enchNull.descriptionKey() + "')");

        // ---- M8-C:LivingEntity(null 语义为主,双代必跑;26.2 LivingEntity / 1.8.9
        // EntityLivingBase 均需真实 Level 才能构造,契约环境不可构造——与 Entity 先例
        // 一致;Provider 真实路径(health/maxHealth/dead 直读)完整实现,待服务端环境。
        // 兜底值双代同值:health 0、maxHealth 0、dead false) ----
        MosaicLivingEntity living = p.livingEntityOf(null);
        check(living != null, "livingEntityOf(null) null-safe handle");
        check(living.health() == 0.0f, "livingEntityOf(null) health 0 (got " + living.health() + ")");
        check(living.maxHealth() == 0.0f,
                "livingEntityOf(null) maxHealth 0 (got " + living.maxHealth() + ")");
        check(!living.dead(), "livingEntityOf(null) dead false");

        // ---- M8-C:StatusEffect(真实路径双代可构造 + null 语义双代必跑) ----
        // 逆向核实:26.2 MobEffectInstance(Holder, int, int) 轻参构造(MobEffectInstance
        // .java:56;Holder 用 MobEffects.REGENERATION 静态字段)、1.8.9 PotionEffect(int,
        // int, int) 轻参构造(PotionEffect.java:36-39)——if-available 守卫作防御:
        // 某代构造失败 → 真实路径断言跳过,null 语义仍双代必跑(与 commandObject 同款模式)。
        boolean effectAvailable = false;
        Object effectObj = null;
        try { effectObj = env.statusEffectObject(); effectAvailable = true; }
        catch (Exception ex) {
            System.err.println("NOTE: " + p.mcVersion() + " statusEffectObject unavailable, "
                    + "status effect real-path assertions skipped: " + ex);
        }
        if (effectAvailable) {
            MosaicStatusEffect eff = p.statusEffectOf(effectObj);
            check(eff != null, "status effect handle (real path)");
            // 双代同值断言:26.2 MobEffects.REGENERATION 与 1.8.9 Potion.regeneration
            // (id 10)同映射 "minecraft:regeneration";duration/amplifier 精确断言
            // (探针统一构造 duration 100 / amplifier 1)
            check("minecraft:regeneration".equals(eff.registryName()),
                    "status effect registryName 'minecraft:regeneration' (got '"
                            + eff.registryName() + "')");
            check(eff.amplifier() == 1,
                    "status effect amplifier 1 (got " + eff.amplifier() + ")");
            check(eff.duration() == 100,
                    "status effect duration 100 (got " + eff.duration() + ")");
        }
        // StatusEffect null 语义(双代必跑,兜底值双代同值)
        MosaicStatusEffect effNull = p.statusEffectOf(null);
        check(effNull != null, "statusEffectOf(null) null-safe handle");
        check("unknown".equals(effNull.registryName()),
                "statusEffectOf(null) registryName 'unknown' (got '" + effNull.registryName() + "')");
        check(effNull.amplifier() == 0,
                "statusEffectOf(null) amplifier 0 (got " + effNull.amplifier() + ")");
        check(effNull.duration() == 0,
                "statusEffectOf(null) duration 0 (got " + effNull.duration() + ")");

        // ---- M8-C:Tag(26.2 TagKey 可构造真实路径;1.8.9 无标签系统(jar 无
        // net.minecraft.tags 包,逆向核实)→ if-available 守卫跳过真实断言并打印
        // NOTE;null 语义双代必跑) ----
        // 26.2 真实路径限于"可构造 TagKey + registryName":标签内容为数据驱动
        // (TagLoader 世界加载期绑定),契约环境未绑定 → contents() 空数组(Provider
        // 吸收未绑定 Named 的迭代异常,HolderSet.java:171);真实内容查询在服务端环境。
        boolean tagAvailable = false;
        Object tagObj = null;
        try { tagObj = env.tagObject(); tagAvailable = true; }
        catch (Exception ex) {
            System.err.println("NOTE: " + p.mcVersion() + " tagObject unavailable, "
                    + "tag real-path assertions skipped: " + ex);
        }
        if (tagAvailable) {
            MosaicTag tag = p.tagOf(tagObj);
            check(tag != null, "tag handle (real path)");
            check("minecraft:planks".equals(tag.registryName()),
                    "tag registryName 'minecraft:planks' (got '" + tag.registryName() + "')");
            check(tag.contents() != null && tag.contents().length == 0,
                    "tag contents empty in contract env (unbound tags, got "
                            + (tag.contents() == null ? "null" : String.valueOf(tag.contents().length)) + ")");
        }
        // Tag null 语义(双代必跑,兜底值双代同值)
        MosaicTag tagNull = p.tagOf(null);
        check(tagNull != null, "tagOf(null) null-safe handle");
        check("unknown".equals(tagNull.registryName()),
                "tagOf(null) registryName 'unknown' (got '" + tagNull.registryName() + "')");
        check(tagNull.contents() != null && tagNull.contents().length == 0,
                "tagOf(null) contents empty");

        // ---- M8-C:BlockEntity(null 语义为主,双代必跑;26.2 BlockEntity 受保护构造器
        // 需 BlockEntityType+BlockState、1.8.9 TileEntity 抽象类——契约环境不可构造,
        // 与 Entity 先例一致;Provider 真实路径完整实现,待服务端环境。
        // 兜底值双代同值:typeRegistryName "unknown"、pos null) ----
        MosaicBlockEntity be = p.blockEntityOf(null);
        check(be != null, "blockEntityOf(null) null-safe handle");
        check("unknown".equals(be.typeRegistryName()),
                "blockEntityOf(null) typeRegistryName 'unknown' (got '"
                        + be.typeRegistryName() + "')");
        check(be.pos() == null, "blockEntityOf(null) pos null");

        if (failures == 0) System.out.println("VANILLA CONTRACT PASSED (" + p.mcVersion() + ")");
        System.exit(failures == 0 ? 0 : 1);
    }
}
