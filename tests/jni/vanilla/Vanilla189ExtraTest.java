import mosaic.MosaicApiException;
import mosaic.vanilla.*;
import mosaic.vanilla.internal.ReflectUtil;

/** 1.8.9 成功注册路径契约测试(1.8.9 独有,只跑于 ci/run_vanilla_contract_189.sh;
 *  26.2 注册表冻结、契约环境不可真实注册(m8-a 裁决),成功路径不重复)。
 *  真实 MCP jar 环境:新 Block(Material, MapColor) / 新 Item() 可构造(逆向核实
 *  Block.java / Item.java,实测见 task-m8-a-report.md §2),blockRegistry/itemRegistry
 *  可写 → Provider registerBlock/registerItem 真实路径全链路断言:
 *  id 分配成功、按名查回、数字 id 语义(entry id == vanilla getIDForObject,经
 *  name(id) byId 回环)、句柄注册名一致、两次注册 id 递增、重复值守卫(3.2)消息
 *  含 id 与两个注册名 + 注册表零污染。
 *  注:本测试向全局注册表写入测试条目(mosaic:* 命名,不撞 vanilla),仅在独立测试
 *  JVM 进程内存在,进程退出即清(与 m8-a 开发期探针同款,无持久影响)。 */
public class Vanilla189ExtraTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }

    public static void main(String[] args) throws Exception {
        Vanilla189Env env = new Vanilla189Env();
        MosaicProvider p = env.provider();
        MosaicRegistry reg = p.registryOf(env.registryObject());   // Block.blockRegistry
        MosaicRegistry itemReg = p.registryOf(
                ReflectUtil.fieldStatic("net.minecraft.item.Item", "itemRegistry"));

        // ---- 方块成功注册路径:新 Block(Material.rock, MapColor.stoneColor) ----
        Object newBlock = ReflectUtil.callConstructor("net.minecraft.block.Block",
                ReflectUtil.fieldStatic("net.minecraft.block.material.Material", "rock"),
                ReflectUtil.fieldStatic("net.minecraft.block.material.MapColor", "stoneColor"));
        MosaicRegistryEntry be = reg.registerBlock("mosaic:probe_block", newBlock);
        check(be.id() >= 0, "block entry id >= 0 (got " + be.id() + ")");
        check(reg.id("mosaic:probe_block") == be.id(),
                "block lookup by name == entry id (" + reg.id("mosaic:probe_block")
                        + " vs " + be.id() + ")");
        check("mosaic:probe_block".equals(reg.name(be.id())),
                "block name by id roundtrip (got '" + reg.name(be.id()) + "')");
        // 数字 id 语义:entry id == vanilla getIDForObject(同一 id 映射)
        Object blockReg = ReflectUtil.fieldStatic("net.minecraft.block.Block", "blockRegistry");
        int vanillaBlockId = (Integer) ReflectUtil.call(blockReg, "getIDForObject", newBlock);
        check(vanillaBlockId == be.id(),
                "block entry id == vanilla getIDForObject (" + vanillaBlockId + " vs " + be.id() + ")");
        check("mosaic:probe_block".equals(p.blockOf(newBlock).registryName()),
                "blockOf(registered).registryName (got '" + p.blockOf(newBlock).registryName() + "')");
        // 第二次注册:新对象新名 → 不同 id(id 分配递增)
        Object newBlock2 = ReflectUtil.callConstructor("net.minecraft.block.Block",
                ReflectUtil.fieldStatic("net.minecraft.block.material.Material", "rock"),
                ReflectUtil.fieldStatic("net.minecraft.block.material.MapColor", "stoneColor"));
        MosaicRegistryEntry be2 = reg.registerBlock("mosaic:probe_block2", newBlock2);
        check(be2.id() >= 0 && be2.id() != be.id(),
                "second block gets a different id (" + be2.id() + " vs " + be.id() + ")");

        // ---- 物品成功注册路径:新 Item()(itemRegistry 为普通 RegistryNamespaced,
        //      无 air 默认值;注册句柄与方块句柄隔离,各自锚定类型的注册表) ----
        Object newItem = ReflectUtil.callConstructor("net.minecraft.item.Item");
        MosaicRegistryEntry ie = itemReg.registerItem("mosaic:probe_item", newItem);
        check(ie.id() >= 0, "item entry id >= 0 (got " + ie.id() + ")");
        check(itemReg.id("mosaic:probe_item") == ie.id(),
                "item lookup by name == entry id (" + itemReg.id("mosaic:probe_item")
                        + " vs " + ie.id() + ")");
        check("mosaic:probe_item".equals(itemReg.name(ie.id())),
                "item name by id roundtrip (got '" + itemReg.name(ie.id()) + "')");
        Object itemRegObj = ReflectUtil.fieldStatic("net.minecraft.item.Item", "itemRegistry");
        int vanillaItemId = (Integer) ReflectUtil.call(itemRegObj, "getIDForObject", newItem);
        check(vanillaItemId == ie.id(),
                "item entry id == vanilla getIDForObject (" + vanillaItemId + " vs " + ie.id() + ")");
        check("mosaic:probe_item".equals(p.itemOf(newItem).registryName()),
                "itemOf(registered).registryName (got '" + p.itemOf(newItem).registryName() + "')");

        // ---- 重复值守卫(3.2,真实环境消息核实):同对象换新名 → MosaicApiException,
        //     消息含 id 与两个注册名(修前:Guava BiMap 抛 IllegalArgumentException 时
        //     ObjectIntIdentityMap 已静默改写值→id,注册表损坏) ----
        int probeId = be.id();
        boolean dupThrew = false;
        String dupMsg = "";
        try { reg.registerBlock("mosaic:probe_block_dup", newBlock); }
        catch (MosaicApiException ex) {
            dupThrew = true;
            dupMsg = ex.getMessage() == null ? "" : ex.getMessage();
        }
        check(dupThrew, "re-register registered block under new name throws MosaicApiException");
        check(dupMsg.contains("'mosaic:probe_block' (id " + probeId)
                        && dupMsg.contains("'mosaic:probe_block_dup'"),
                "duplicate-value message has existing name+id and new name (got '" + dupMsg + "')");
        check(reg.id("mosaic:probe_block") == probeId && reg.id("mosaic:probe_block_dup") == -1,
                "duplicate-value attempt leaves registry intact (probe " + probeId + " -> "
                        + reg.id("mosaic:probe_block") + ", dup name -> "
                        + reg.id("mosaic:probe_block_dup") + ")");
        // 物品侧重复值守卫(同一 registerEntry 路径)
        int probeItemId = ie.id();
        boolean dupItemThrew = false;
        try { itemReg.registerItem("mosaic:probe_item_dup", newItem); }
        catch (MosaicApiException ex) { dupItemThrew = true; }
        check(dupItemThrew, "re-register registered item under new name throws MosaicApiException");
        check(itemReg.id("mosaic:probe_item") == probeItemId,
                "duplicate-value item attempt leaves existing id intact (probe " + probeItemId
                        + " -> " + itemReg.id("mosaic:probe_item") + ")");

        if (failures == 0) System.out.println("VANILLA 189 EXTRA PASSED (1.8.9)");
        System.exit(failures == 0 ? 0 : 1);
    }
}
