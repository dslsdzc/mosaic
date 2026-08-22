package mosaic.vanilla.internal;

import mosaic.MosaicApiException;
import mosaic.MosaicHandleException;
import mosaic.vanilla.*;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.lang.ref.WeakReference;
import java.lang.reflect.Proxy;
import java.util.*;

/** 1.8.9 Provider:与 Vanilla262Provider 同结构(反射 + 版本映射表)。句柄持有原版引用,
 *  读取时经 ReflectUtil 转换;类名/方法名/字段名全部取自 VersionMap_1_8_9.properties,
 *  不硬编码版本 API。方法签名核实:~/minecraft1.8.9/mcp918/src/minecraft 逆向源码
 *  (记录见 task-m5-7-report.md)。
 *
 *  1.8.9 语义差异吸收(与 26.2 的差异全在此):
 *  - registryName:Item.itemRegistry/Block.blockRegistry.getNameForObject(item/block)
 *    → ResourceLocation.toString() = "minecraft:<name>"(1.8.9 有注册表名映射,非合成);
 *  - components:MosaicComponents 空实现(1.8.9 无组件系统);
 *  - NBT:getString/getInteger 直接返回 String/int(缺失 ""/0),无 Optional 解包;
 *  - world.dimension():World.provider(public 字段).getDimensionId() → int,
 *    按 dimensionId 合成维度名(-1→minecraft:the_nether, 0→minecraft:overworld,
 *    1→minecraft:the_end, 其他→minecraft:dim_<id>);
 *  - world.tick():1.8.9 World.tick() 无参(World.java:2476 / WorldServer.java:166);
 *  - world.entities():public 字段 loadedEntityList(26.2 为方法 getEntities);
 *  - registry:Block.blockRegistry 为 RegistryNamespacedDefaultedByKey(默认值 air),
 *    未注册键返回默认值 → id() 先 containsKey 存在性守卫(与 26.2 同款);
 *  - entity.type:1.8.9 无 EntityType 注册表 → 由类名合成
 *    ("net.minecraft.entity.monster.EntityZombie" → "minecraft:zombie"),best-effort;
 *  - attribute:1.8.9 属性常量在 net.minecraft.entity.SharedMonsterAttributes
 *    ("generic.maxHealth" 等);"minecraft:max_health" → camelCase → 字段名,仅
 *    EntityLivingBase 有 getEntityAttribute(非 Living 实体抛句柄异常),best-effort;
 *  - 契约环境无运行中服务端:worldOf 对非 World 对象(维度令牌 Integer 或 null)
 *    返回 null-safe 句柄,真实 World 路径照常实现(26.2 先例)。 */
public final class Vanilla189Provider implements MosaicProvider {
    private final Properties map = new Properties();
    /** itemStackOf 生成的句柄 → 原版 ItemStack 弱引用(itemStackOf 幂等重建,setItem 回写用)。 */
    private final Map<MosaicItemStack, WeakReference<Object>> stacks = new WeakHashMap<>();
    /** blockStateOf 生成的句柄 → 原版 IBlockState 弱引用(setBlock 回写/取原版状态用)。 */
    private final Map<MosaicBlockState, WeakReference<Object>> states = new WeakHashMap<>();

    public Vanilla189Provider() {
        try { map.load(getClass().getResourceAsStream("VersionMap_1_8_9.properties")); }
        catch (Exception e) { throw new IllegalStateException("version map missing", e); }
    }
    public String providerId() { return "vanilla-1.8.9"; }
    public String mcVersion() { return "1.8.9"; }
    public boolean supportsApi(int min, int max) { return true; }

    /* ---------- 内部工具:类名/方法名/字段名全部经映射表 ---------- */

    private String cls(String key) { return map.getProperty(key + ".class"); }
    private String m(String key) { return map.getProperty(key); }

    /** 调用失败抛 MosaicHandleException(接口语义:未知属性等抛句柄异常)。 */
    private static Object call(Object t, String method, Object... args) {
        try { return ReflectUtil.call(t, method, args); }
        catch (Exception e) { throw new MosaicHandleException(method + " failed: " + e); }
    }
    private static Object callSafe(Object t, String method, Object... args) {
        try { return ReflectUtil.call(t, method, args); } catch (Exception e) { return null; }
    }
    private static int intOf(Object o) {
        return o instanceof Number n ? n.intValue() : 0;
    }

    /** 1.8.9 注册表名:registry.getNameForObject(obj) → ResourceLocation.toString()。
     *  1.8.9 有注册表名映射(非合成);未注册返回 null → "unknown"。 */
    private String registryNameVia(Object registry, String nameMethod, Object target) {
        try {
            Object key = ReflectUtil.call(registry, map.getProperty(nameMethod), target);
            return key == null ? "unknown" : key.toString();
        } catch (Exception e) { return "unknown"; }
    }

    private MosaicEntityId entityIdOf(final Object e) {
        return new MosaicEntityId() { public int value() { return intOf(call(e, m("entity.id"))); } };
    }

    /* ---------- MosaicProvider 接口实现 ---------- */

    public MosaicBlock blockOf(Object vanillaBlock) {
        final Object b = vanillaBlock;
        final Object reg = field("block.registry");   // Block.blockRegistry
        return new MosaicBlock() {
            public MosaicBlockState state() { return blockStateOf(call(b, m("block.defaultstate"))); }
            public String registryName() { return registryNameVia(reg, "block.registryname", b); }
        };
    }

    public MosaicBlockState blockStateOf(Object vanillaBlockState) {
        final Object st = vanillaBlockState;
        MosaicBlockState h = new MosaicBlockState() {
            public MosaicBlock block() { return blockOf(call(st, m("blockstate.block"))); }
            public String[] propertyNames() {
                try {
                    Map<?, ?> props = propertiesOf(st);
                    if (props == null) return new String[0];
                    List<String> out = new ArrayList<>();
                    for (Object p : props.keySet()) {
                        Object pn = callSafe(p, m("property.name"));
                        out.add(pn == null ? String.valueOf(p) : pn.toString());
                    }
                    return out.toArray(new String[0]);
                } catch (Exception e) { return new String[0]; }
            }
            public String property(String name) {
                try {
                    Map<?, ?> props = propertiesOf(st);
                    if (props != null)
                        for (Map.Entry<?, ?> en : props.entrySet()) {
                            Object pn = callSafe(en.getKey(), m("property.name"));
                            if (name != null && name.equals(String.valueOf(pn)))
                                return String.valueOf(en.getValue());
                        }
                    throw new NoSuchMethodException("property " + name);
                } catch (Exception e) { throw new MosaicHandleException("property " + name + ": " + e); }
            }
        };
        if (st != null) states.put(h, new WeakReference<>(st));
        return h;
    }

    /** MosaicBlockState 句柄 → 原版 IBlockState(弱引用表溯源;不可溯源抛句柄异常)。
     *  setBlock 写路径回写用(与 itemStackOf/stacks 的 itemStack 回写同款)。 */
    private Object stateOf(MosaicBlockState h) {
        WeakReference<Object> ref = states.get(h);
        Object st = ref == null ? null : ref.get();
        if (st == null) throw new MosaicHandleException("setBlock: state handle not backed by a vanilla IBlockState");
        return st;
    }

    /** 1.8.9 IBlockState.getProperties() 返回 ImmutableMap<IProperty,Comparable>
     *  (26.2 为 Collection)——双形态吸收。 */
    private Map<?, ?> propertiesOf(Object st) throws Exception {
        Object r = ReflectUtil.call(st, m("blockstate.properties"));
        return r instanceof Map ? (Map<?, ?>) r : null;
    }

    public MosaicItem itemOf(Object vanillaItem) {
        final Object it = vanillaItem;
        final Object reg = field("item.registry");   // Item.itemRegistry
        return new MosaicItem() {
            public String registryName() { return registryNameVia(reg, "item.registryname", it); }
            public int maxStackSize() {
                try { return intOf(call(it, m("item.maxstack"))); } catch (Exception e) { return 1; }
            }
            public MosaicComponents components() {
                // 1.8.9 无组件系统:空实现(keys 空/get null/with 幂等返回自身)
                return emptyComponents();
            }
            public int maxDamage() {
                // 1.8.9 Item.getMaxDamage() 直接返回 maxDamage 字段(默认 0,不可损坏)
                try { return intOf(call(it, m("item.maxdamage"))); } catch (Exception e) { return 0; }
            }
            public boolean damageable() {
                // 1.8.9 Item.isDamageable() = maxDamage > 0 && !hasSubtypes(Item.java:207)
                try {
                    Object r = call(it, m("item.isdamageable"));
                    return r instanceof Boolean b && b;
                } catch (Exception e) { return maxDamage() > 0; }
            }
        };
    }

    /** 1.8.9 组件空语义(版本差异吸收:26.2 为 DataComponentMap 真实映射)。 */
    private MosaicComponents emptyComponents() {
        return new MosaicComponents() {
            public String[] keys() { return new String[0]; }
            public byte[] get(String key) { return null; }
            public MosaicComponents with(String key, byte[] value) { return this; }
        };
    }

    public MosaicItemStack itemStackOf(Object vanillaItemStack) {
        final Object s = vanillaItemStack;
        MosaicItemStack h = new MosaicItemStack() {
            public MosaicItem item() { return itemOf(call(s, m("itemstack.item"))); }
            public int count() { return intOf(fieldOf(s, "itemstack.count")); }  // 1.8.9 public 字段
            public MosaicItemStack copy() { return itemStackOf(call(s, m("itemstack.copy"))); }
        };
        if (s != null) stacks.put(h, new WeakReference<>(s));
        return h;
    }

    public MosaicWorld worldOf(Object vanillaWorld) {
        final Object w = vanillaWorld;
        final String tokenDim = dimensionToken(w);   // 真实 World 或维度令牌(Integer dimensionId)
        return new MosaicWorld() {
            public String dimension() { return tokenDim != null ? tokenDim : "unknown"; }
            public MosaicBlockState getBlock(MosaicBlockPos pos) {
                if (w == null || pos == null) return null;
                try {
                    Object bp = ReflectUtil.callConstructor(cls("blockpos"), pos.x(), pos.y(), pos.z());
                    Object st = ReflectUtil.call(w, m("world.blockstate"), bp);
                    return st == null ? null : blockStateOf(st);
                } catch (Exception e) { return null; }   // 非 World 令牌/越界 → null(契约语义)
            }
            public MosaicEntity[] entities() {
                if (w == null) return new MosaicEntity[0];
                try {
                    Object list = ReflectUtil.field(w, m("world.entities"));   // loadedEntityList 字段
                    if (!(list instanceof Collection)) return new MosaicEntity[0];
                    List<MosaicEntity> out = new ArrayList<>();
                    for (Object e : (Collection<?>) list) out.add(entityOf(e));
                    return out.toArray(new MosaicEntity[0]);
                } catch (Exception e) { return new MosaicEntity[0]; }
            }
            public MosaicEntity entityById(int entityId) {
                if (w == null) return null;
                try {
                    Object e = ReflectUtil.call(w, m("world.entity"), entityId);
                    return e == null ? null : entityOf(e);
                } catch (Exception e) { return null; }
            }
            public void tick() {
                try { ReflectUtil.call(w, m("world.tick")); }   // 1.8.9 World.tick() 无参
                catch (Exception ignored) { }
            }
            public void save() {
                // 1.8.9:WorldServer.saveAllChunks(boolean, IProgressUpdate);客户端 World/令牌无此方法 → 跳过
                try { ReflectUtil.call(w, m("world.save"), false, null); }
                catch (Exception ignored) { }
            }
            public long gameTime() {
                if (w == null) return 0;
                Object t = callSafe(w, m("world.gametime"));
                return t instanceof Number n ? n.longValue() : 0L;
            }
            public boolean setBlock(MosaicBlockPos pos, MosaicBlockState state) {
                // 写路径:World.setBlockState(BlockPos, IBlockState)(内部 flags=3,与
                // 26.2 Provider 显式传 3 对齐——两代默认更新语义一致)。句柄未持有真实
                // World(null 或维度令牌 Integer)时抛 MosaicHandleException——写路径不
                // 静默(与读路径 getBlock 的 null 语义区分;真实路径待运行中服务端环境)。
                if (w == null) throw new MosaicHandleException("setBlock: world handle has no live World");
                if (pos == null || state == null)
                    throw new MosaicHandleException("setBlock: pos and state must be non-null");
                try {
                    Object bp = ReflectUtil.callConstructor(cls("blockpos"), pos.x(), pos.y(), pos.z());
                    Object st = stateOf(state);
                    Object r = ReflectUtil.call(w, m("world.setblockstate"), bp, st);
                    return r instanceof Boolean b && b;
                } catch (MosaicHandleException e) { throw e; }
                catch (Exception e) { throw new MosaicHandleException("setBlock: " + e); }
            }
        };
    }

    /** 维度资源名:真实 World → provider.getDimensionId() → 合成;维度令牌(Integer)直取。
     *  均失败 → null。合成规则见 composeDimensionName。 */
    private String dimensionToken(Object w) {
        if (w == null) return null;
        if (w instanceof Number n) return composeDimensionName(n.intValue());   // 维度令牌
        try {
            Object provider = ReflectUtil.field(w, m("world.dimension"));       // World.provider
            Object dimId = provider == null ? null : callSafe(provider, m("world.dimname"));
            return dimId instanceof Number n ? composeDimensionName(n.intValue()) : null;
        } catch (Exception e) { return null; }
    }

    /** 维度名合成规则(1.8.9 无维度资源名概念,记录于此):
     *  dimensionId -1 → minecraft:the_nether;0 → minecraft:overworld;1 → minecraft:the_end;
     *  其他 → minecraft:dim_<id>。 */
    private String composeDimensionName(int dimensionId) {
        switch (dimensionId) {
            case -1: return "minecraft:the_nether";
            case 0:  return "minecraft:overworld";
            case 1:  return "minecraft:the_end";
            default: return "minecraft:dim_" + dimensionId;
        }
    }

    public MosaicEntity entityOf(Object vanillaEntity) {
        final Object e = vanillaEntity;
        return new MosaicEntity() {
            public MosaicEntityId id() { return entityIdOf(e); }
            public MosaicEntityType type() {
                return new MosaicEntityType() {
                    public String registryName() {
                        // 1.8.9 无 EntityType 注册表:由类名合成 "Entity<Name>" → "minecraft:<name>"
                        String n = e == null ? "" : e.getClass().getSimpleName();
                        if (n.startsWith("Entity")) n = n.substring(6);
                        return n.isEmpty() ? "unknown" : "minecraft:" + n.toLowerCase(Locale.ROOT);
                    }
                };
            }
            public double x() { return ((Number) fieldOf(e, "entity.x")).doubleValue(); }
            public double y() { return ((Number) fieldOf(e, "entity.y")).doubleValue(); }
            public double z() { return ((Number) fieldOf(e, "entity.z")).doubleValue(); }
            public double attribute(String name) {
                try {
                    if (name == null) throw new NoSuchMethodException("attribute null");
                    // "minecraft:max_health" → "generic.maxHealth" → SharedMonsterAttributes.maxHealth
                    String mcp = "generic." + camelCase(stripNamespace(name));
                    Object attr = ReflectUtil.fieldStatic(cls("sharedmonsterattributes"), mcp);
                    if (attr == null) throw new NoSuchMethodException("attribute " + name);
                    Object inst = ReflectUtil.call(e, m("entity.attribute"), attr);
                    return inst == null ? Double.NaN
                            : ((Number) ReflectUtil.call(inst, m("attrinstance.value"))).doubleValue();
                } catch (Exception ex) { throw new MosaicHandleException("attribute " + name + ": " + ex); }
            }
        };
    }

    public MosaicPlayer playerOf(Object vanillaPlayer) {
        final Object pl = vanillaPlayer;
        return new MosaicPlayer() {
            public MosaicEntityId entityId() { return entityIdOf(pl); }
            public String name() {
                try {
                    Object profile = ReflectUtil.call(pl, m("player.profile"));
                    Object n = ReflectUtil.call(profile, m("player.name"));
                    return n == null ? "" : n.toString();
                } catch (Exception e) { return ""; }
            }
            public int gameMode() {
                try {
                    // EntityPlayerMP.theItemInWorldManager(public 字段).getGameType().getID()
                    Object mgr = ReflectUtil.field(pl, m("player.mode"));
                    Object gt = ReflectUtil.call(mgr, m("playermgr.gametype"));
                    return intOf(ReflectUtil.call(gt, m("gametype.id")));
                } catch (Exception e) { return -1; }   // 非 EntityPlayerMP 无 gameMode
            }
            public boolean online() {
                try { return ReflectUtil.field(pl, m("player.connection")) != null; }  // playerNetServerHandler
                catch (Exception e) { return false; }
            }
        };
    }

    public MosaicInventory inventoryOf(Object vanillaInventory) {
        final Object inv = vanillaInventory;
        return new MosaicInventory() {
            public int slotCount() { return intOf(call(inv, m("container.size"))); }
            public MosaicInventorySlot slot(final int index) {
                return new MosaicInventorySlot() {
                    public int index() { return index; }
                    public MosaicItemStack item() { return getItem(index); }
                    public boolean isEmpty() { return item() == null; }
                };
            }
            public MosaicItemStack getItem(int index) {
                try {
                    Object st = ReflectUtil.call(inv, m("container.item"), index);
                    if (st == null) return null;
                    int size = intOf(ReflectUtil.field(st, m("itemstack.count")));   // 1.8.9:stackSize==0 为空
                    return size <= 0 ? null : itemStackOf(st);
                } catch (Exception e) { return null; }
            }
            public void setItem(int index, MosaicItemStack stack) {
                try {
                    Object vanilla = null;
                    if (stack != null) {
                        WeakReference<Object> ref = stacks.get(stack);
                        vanilla = ref == null ? null : ref.get();
                        if (vanilla == null) {   // 句柄不可溯源时按注册表名重建 ItemStack
                            Object item = registryValue(stack.item().registryName());
                            vanilla = ReflectUtil.callConstructor(cls("itemstack"), item, stack.count(), 0);
                        }
                    }
                    ReflectUtil.call(inv, m("container.setitem"), index, vanilla);
                } catch (Exception e) { throw new MosaicHandleException("setItem " + index + ": " + e); }
            }
            public int size() { return slotCount(); }
        };
    }

    /** 注册表静态字段(Block.blockRegistry / Item.itemRegistry);类键 = "block.registry" → "block.class"。 */
    private Object field(String key) {
        String clsKey = key.substring(0, key.indexOf('.')) + ".class";
        try { return ReflectUtil.fieldStatic(map.getProperty(clsKey), m(key)); }
        catch (Exception e) { throw new MosaicHandleException("field " + key + ": " + e); }
    }

    /** 注册表值经名称解析("minecraft:stone" → Item):Item.itemRegistry.getObject(ResourceLocation)。 */
    private Object registryValue(String name) {
        try {
            Object rl = ReflectUtil.callConstructor(cls("resloc"), name);
            return rl == null ? null : ReflectUtil.call(field("item.registry"), m("registry.value"), rl);
        } catch (Exception e) { return null; }
    }

    public MosaicRegistry registryOf(Object vanillaRegistry) {
        final Object reg = vanillaRegistry;
        return new MosaicRegistry() {
            public int id(String registryName) { return registryIdOf(reg, registryName); }
            public String name(int id) {
                try {
                    if (id < 0) return null;
                    Object value = ReflectUtil.call(reg, m("registry.byid"), id);
                    if (value == null) return null;
                    // DefaultedByKey:byId(未注册 id) 返回默认值(air),getId 回环校验真实注册
                    if (intOf(ReflectUtil.call(reg, m("registry.id"), value)) != id) return null;
                    Object key = ReflectUtil.call(reg, m("registry.key"), value);
                    return key == null ? null : key.toString();   // ResourceLocation → "minecraft:stone"
                } catch (Exception e) { return null; }
            }
            public MosaicRegistryEntry registerBlock(String registryName, Object vanillaBlock) {
                return registerEntry("block.registry", "block", registryName, vanillaBlock);
            }
            public MosaicRegistryEntry registerItem(String registryName, Object vanillaItem) {
                return registerEntry("item.registry", "item", registryName, vanillaItem);
            }
        };
    }

    /** 名 → id 查询(1.8.9 DefaultedByKey 存在性守卫:未注册名返回默认值(air),
     *  先 containsKey 判存在再取值;共享给 id() 与 registerEntry 的条目 id 计算)。 */
    private int registryIdOf(Object reg, String registryName) {
        try {
            if (registryName == null) return -1;
            Object rl = ReflectUtil.callConstructor(cls("resloc"), registryName);
            if (rl == null) return -1;
            if (!(ReflectUtil.call(reg, m("registry.containskey"), rl) instanceof Boolean b) || !b)
                return -1;
            Object value = ReflectUtil.call(reg, m("registry.value"), rl);
            return value == null ? -1 : intOf(ReflectUtil.call(reg, m("registry.id"), value));
        } catch (Exception e) { return -1; }
    }

    /** 注册写路径(1.8.9 适配:注册名 → 注册表,Block.blockRegistry / Item.itemRegistry
     *  数字 ID 映射)。注册目标 = 该类型的规范注册表(与句柄包装的注册表对象无关——
     *  registerBlock/registerItem 各自锚定类型的注册表)。流程:名字/对象校验 →
     *  重复守卫(containsKey——1.8.9 RegistrySimple.putObject 对重名仅 debug 日志
     *  覆盖不抛错,守卫先拦截,与 M7-B 命令重名守卫同款)→ 数字 id 分配(注册表
     *  无自动分配 API,id 由 Provider 取首个空闲 id)→ RegistryNamespaced.register
     *  (id, rl, value)。逆向核实:RegistryNamespaced.java:18-22 / ObjectIntIdentityMap
     *  .java(put/get/getByValue);defaulted 注册表 getObjectById(未注册 id) 返回
     *  默认值(air),空闲判定经 getId 回环校验(与 name(id) 回环同款,两形态统一)。 */
    private MosaicRegistryEntry registerEntry(String regFieldKey, String what, String registryName, Object vanilla) {
        if (registryName == null || registryName.isEmpty())
            throw new MosaicApiException(what + " registry name must be non-empty");
        if (vanilla == null) throw new MosaicApiException("vanilla " + what + " must be non-null");
        try {
            Object reg = field(regFieldKey);   // Block.blockRegistry / Item.itemRegistry
            Object rl = ReflectUtil.callConstructor(cls("resloc"), registryName);
            if (rl == null) throw new MosaicApiException("invalid registry name: " + registryName);
            if (ReflectUtil.call(reg, m("registry.containskey"), rl) instanceof Boolean b && b)
                throw new MosaicApiException(what + " already registered: " + registryName);
            int id = nextFreeId(reg);
            ReflectUtil.call(reg, m("registry.register"), id, rl, vanilla);
            return entryOf(reg, registryName);
        } catch (MosaicApiException e) { throw e; }
        catch (Exception e) { throw new MosaicApiException("register " + what + " '" + registryName + "': " + e); }
    }

    /** 首个空闲数字 id:注册表无 nextFreeId/maxId 访问器(逆向核实 1.8.9 源码),
     *  以 getObjectById(id) 回环判占用——occupied ⇔ v != null && getId(v) == id。
     *  blockRegistry(DefaultedByKey):未注册 id → 默认值 air,getId(air)=0 != id → 空闲;
     *  itemRegistry(普通 RegistryNamespaced):未注册 id → null → 空闲。
     *  块注册表 id 稠密(0..N),分配结果 = N+1,与 vanilla 逐个 id 注册形态一致。 */
    private int nextFreeId(Object reg) throws Exception {
        for (int id = 0; ; id++) {
            Object v = ReflectUtil.call(reg, m("registry.byid"), id);
            if (v == null) return id;
            if (intOf(ReflectUtil.call(reg, m("registry.id"), v)) != id) return id;
        }
    }

    private MosaicRegistryEntry entryOf(final Object reg, final String name) {
        return new MosaicRegistryEntry() {
            public int id() { return registryIdOf(reg, name); }
            public String registryName() { return name; }
        };
    }

    public MosaicNbt nbtOf(Object vanillaNbt) {
        final Object tag = vanillaNbt;
        return new MosaicNbt() {
            public MosaicNbtCompound compound() {
                return new MosaicNbtCompound() {
                    public boolean contains(String key) {
                        Object r = callSafe(tag, m("nbt.contains"), key);
                        return r instanceof Boolean b && b;
                    }
                    public String getString(String key) {
                        // 1.8.9:直接返回 String(缺失 ""),无 Optional
                        Object r = callSafe(tag, m("nbt.getstring"), key);
                        return r == null ? "" : r.toString();
                    }
                    public int getInt(String key) {
                        Object r = callSafe(tag, m("nbt.getint"), key);
                        return r instanceof Number n ? n.intValue() : 0;
                    }
                    public void putString(String key, String value) { call(tag, m("nbt.putstring"), key, value); }
                    public void putInt(String key, int value) { call(tag, m("nbt.putint"), key, value); }
                    public String[] keys() {
                        Object r = callSafe(tag, m("nbt.keys"));
                        if (r instanceof Set) {
                            List<String> out = new ArrayList<>();
                            for (Object k : (Set<?>) r) out.add(String.valueOf(k));
                            return out.toArray(new String[0]);
                        }
                        return new String[0];
                    }
                    public byte[] toBytes() {
                        try {
                            ByteArrayOutputStream bos = new ByteArrayOutputStream();
                            // 1.8.9 无 writeUncompressed:writeCompressed(gzip)写
                            ReflectUtil.callStatic(cls("nbtio"), m("nbtio.write"), tag, bos);
                            return bos.toByteArray();
                        } catch (Exception e) { return new byte[0]; }
                    }
                };
            }
        };
    }

    /* ---------- Command / Network(M7-B) ---------- */

    /** 命令句柄工厂:包装 1.8.9 命令对象(net.minecraft.command.CommandHandler)。
     *  契约环境可构造性:CommandHandler 隐式无参构造(逆向核实 CommandHandler.java:
     *  无声明构造器,字段内联初始化,无 server 依赖)——真实路径可用;
     *  ServerCommandManager(需 MinecraftServer)不可构造,但契约只用 CommandHandler。
     *  null → null-safe 句柄(registered 空/register no-op)。
     *  register 语义:registerCommand(ICommand) → commandMap(命令表);
     *  execute 需 ICommandSender,契约环境不可用——契约只断言 registered() 列表。 */
    public MosaicCommand commandOf(Object vanillaCommand) {
        if (vanillaCommand == null) return new NullSafeCommand();
        return new HandlerCommand(this, vanillaCommand);
    }

    /** 网络句柄工厂:契约环境无真实 NetHandlerPlayServer(构造需 server/player)→
     *  null 语义为主(与 Entity 先例同款);真实路径在运行中服务端环境可用。 */
    public MosaicNetwork networkOf(Object vanillaNetwork) {
        if (vanillaNetwork == null) return new NullSafeNetwork();
        return new NetHandlerNetwork(this, vanillaNetwork);
    }

    /* ---------- Recipe / Enchantment(M8-B) ---------- */

    /** 配方句柄工厂:1.8.9 IRecipe 为接口,真实实例在 CraftingManager 配方表(类加载即
     *  构造,CraftingManager.java:26-51)——但任务务实决策:Recipe null 语义为主 +
     *  真实路径留服务端(与 Entity 先例一致;26.2 契约环境无配方注册表,双代不对称)。
     *  注册名经类名合成(IRecipe 无注册表名概念):类简单名去 Recipe(s) 前缀 →
     *  snake_case → "minecraft:<name>"(与 EntityType 类名合成同款思路);输出 =
     *  getRecipeOutput();type() 无 RecipeType/RecipeCategory(1.14+ 概念)→ 统一
     *  映射 "minecraft:crafting"(CraftingManager 即工作台配方注册表)。 */
    public MosaicRecipe recipeOf(Object vanillaRecipe) {
        if (vanillaRecipe == null) return new NullSafeRecipe();
        final Object r = vanillaRecipe;
        return new MosaicRecipe() {
            public String registryName() {
                String n = r == null ? "" : r.getClass().getSimpleName();
                if (n.startsWith("Recipes")) n = n.substring(7);
                else if (n.startsWith("Recipe")) n = n.substring(6);
                return n.isEmpty() ? "unknown" : "minecraft:" + snakeCase(n);
            }
            public MosaicItemStack result() {
                try {
                    Object out = ReflectUtil.call(r, m("recipe.output"));
                    return out == null ? null : itemStackOf(out);
                } catch (Exception e) { return null; }
            }
            public String type() { return "minecraft:crafting"; }
        };
    }

    /** 附魔句柄工厂:1.8.9 Enchantment 抽象类,构造需 (int id, ResourceLocation,
     *  int weight, EnumEnchantmentType)(Enchantment.java:100-110,重复 id 抛
     *  IllegalArgumentException;任务预设 "(Rarity,int,int)" 为 1.6-1.7 形态,
     *  逆向核实 1.8.9 非该签名)——真实路径用静态实例(Enchantment.sharpness 等,
     *  类加载即注册;见 Vanilla189Env);null → null-safe 句柄。registryName 经
     *  private static locationEnchantments 逆查(Map<ResourceLocation,Enchantment>,
     *  值身份匹配 → key.toString() = "minecraft:sharpness");maxLevel = getMaxLevel()
     *  (EnchantmentDamage 为 5);descriptionKey = getName()("enchantment.damage.all")。 */
    public MosaicEnchantment enchantmentOf(Object vanillaEnchantment) {
        if (vanillaEnchantment == null) return new NullSafeEnchantment();
        final Object e = vanillaEnchantment;
        return new MosaicEnchantment() {
            public String registryName() {
                try {
                    Object map = ReflectUtil.fieldStatic(cls("enchantment"), m("enchantment.locationmap"));
                    if (map instanceof Map<?, ?> m)
                        for (Map.Entry<?, ?> en : m.entrySet())
                            if (en.getValue() == e) return en.getKey().toString();
                    return "unknown";
                } catch (Exception ex) { return "unknown"; }
            }
            public int maxLevel() {
                try { return intOf(call(e, m("enchantment.maxlevel"))); }
                catch (Exception ex) { return 0; }
            }
            public String descriptionKey() {
                try {
                    Object key = callSafe(e, m("enchantment.name"));
                    return key == null ? "" : key.toString();
                } catch (Exception ex) { return ""; }
            }
        };
    }

    /** null-safe 配方句柄(双代同值):registryName "unknown"、result null、type ""
     *  (契约环境无真实 Recipe → 兜底值;与 entityOf(null) 的 "unknown" 先例同款)。 */
    private static final class NullSafeRecipe implements MosaicRecipe {
        public String registryName() { return "unknown"; }
        public MosaicItemStack result() { return null; }
        public String type() { return ""; }
    }

    /** null-safe 附魔句柄(双代同值):registryName "unknown"、maxLevel 0、descriptionKey ""。 */
    private static final class NullSafeEnchantment implements MosaicEnchantment {
        public String registryName() { return "unknown"; }
        public int maxLevel() { return 0; }
        public String descriptionKey() { return ""; }
    }

    /** null-safe 命令句柄(双代同值):registered() 空、register no-op(与 worldOf 的
     *  null-safe 先例同款)。句柄同时实现 MosaicCommandTree(规范 §5 Command 域双接口)。 */
    private static final class NullSafeCommand implements MosaicCommand, MosaicCommandTree {
        public void register(String name, MosaicCommandHandler handler) { }
        public String[] registered() { return new String[0]; }
    }

    /** 1.8.9 真实路径命令句柄:CommandHandler 命令表(registerCommand → commandMap)。
     *  ICommand 为接口(7 方法 + Comparable<ICommand>),以动态代理实现:
     *  getCommandName → name;processCommand(sender, args) → handler.execute(args);
     *  其余默认(canCommandSenderUseCommand true、aliases/补全空、compareTo 0)。
     *  register:名字校验 + 重名守卫(接口契约:重名抛 MosaicApiException)。 */
    private static final class HandlerCommand implements MosaicCommand, MosaicCommandTree {
        private final Vanilla189Provider p;
        private final Object commandHandler;   // CommandHandler
        HandlerCommand(Vanilla189Provider p, Object commandHandler) { this.p = p; this.commandHandler = commandHandler; }

        public String[] registered() {
            try {
                Object cmds = ReflectUtil.call(commandHandler, p.m("commandhandler.commands"));
                if (!(cmds instanceof Map)) return new String[0];
                List<String> out = new ArrayList<>();
                for (Object k : ((Map<?, ?>) cmds).keySet()) out.add(String.valueOf(k));
                return out.toArray(new String[0]);
            } catch (Exception e) { return new String[0]; }
        }

        public void register(String name, MosaicCommandHandler handler) {
            if (name == null || name.isEmpty()) throw new MosaicApiException("command name must be non-empty");
            if (handler == null) throw new MosaicApiException("command handler must be non-null");
            for (String s : registered())
                if (s.equals(name)) throw new MosaicApiException("command already registered: " + name);
            try {
                Class<?> ic = Class.forName(p.m("icommand.class"));
                Object proxy = Proxy.newProxyInstance(getClass().getClassLoader(), new Class<?>[] { ic },
                        (pr, method, args) -> {
                            String mn = method.getName();
                            if ("getCommandName".equals(mn)) return name;
                            if ("getCommandAliases".equals(mn)) return Collections.emptyList();
                            if ("getCommandUsage".equals(mn)) return "";
                            if ("canCommandSenderUseCommand".equals(mn)) return Boolean.TRUE;
                            if ("addTabCompletionOptions".equals(mn)) return Collections.emptyList();
                            if ("isUsernameIndex".equals(mn)) return Boolean.FALSE;
                            if ("processCommand".equals(mn)) {
                                if (args != null && args.length >= 2 && args[1] instanceof String[] sa) handler.execute(sa);
                                return null;   // void 返回
                            }
                            if ("compareTo".equals(mn)) return 0;
                            if ("toString".equals(mn)) return "MosaicCommand(" + name + ")";
                            if ("hashCode".equals(mn)) return System.identityHashCode(pr);
                            if ("equals".equals(mn)) return pr == args[0];
                            return null;
                        });
                ReflectUtil.call(commandHandler, p.m("commandhandler.register"), proxy);
            } catch (Exception e) {
                throw new MosaicHandleException("register '" + name + "': " + e);
            }
        }
    }

    /** null-safe 网络句柄(双代同值):sendPacket no-op、listener() 非 null、
     *  onPacket 返回 no-op 订阅(契约环境无真实 NetHandler → 兜底值)。 */
    private static final class NullSafeNetwork implements MosaicNetwork {
        public void sendPacket(int playerId, byte[] packetData) { }
        public MosaicPacketListener listener() {
            return new MosaicPacketListener() {
                public AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler) {
                    return new AutoCloseable() { public void close() { } };
                }
            };
        }
    }

    /** 真实路径网络句柄(运行中服务端):持有 NetHandler 引用;sendPacket 的包构造与
     *  编码需服务端环境(契约环境不可达)→ 静默跳过;listener 的 onPacket 做本地
     *  订阅簿记(返回可关闭订阅;真实包分发接入待服务端/原生桥环境)。 */
    private static final class NetHandlerNetwork implements MosaicNetwork {
        private final Vanilla189Provider p;
        private final Object netHandler;   // NetHandlerPlayServer
        private final Map<String, List<MosaicPacketHandler>> subs = new HashMap<>();
        NetHandlerNetwork(Vanilla189Provider p, Object netHandler) { this.p = p; this.netHandler = netHandler; }

        public void sendPacket(int playerId, byte[] packetData) {
            // 1.8.9 playerNetServerHandler.sendPacket(S19Packet...):包构造需运行中服务端,契约环境不可构造 → 静默跳过
        }
        public MosaicPacketListener listener() {
            return new MosaicPacketListener() {
                public AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler) {
                    if (packetTypeName == null || handler == null)
                        return new AutoCloseable() { public void close() { } };
                    final MosaicPacketHandler h = handler;
                    subs.computeIfAbsent(packetTypeName, k -> new ArrayList<>()).add(h);
                    return new AutoCloseable() {
                        public void close() {
                            List<MosaicPacketHandler> list = subs.get(packetTypeName);
                            if (list != null) list.remove(h);
                        }
                    };
                }
            };
        }
    }

    /* ---------- 名称工具 ---------- */

    private static String stripNamespace(String name) {
        int i = name == null ? -1 : name.indexOf(':');
        return i >= 0 ? name.substring(i + 1) : name;
    }

    /** "RecipesArmorDyes" → "armor_dyes"(1.8.9 配方注册名合成:类名 camelCase → snake_case)。 */
    private static String snakeCase(String camel) {
        StringBuilder sb = new StringBuilder();
        for (char c : camel.toCharArray()) {
            if (Character.isUpperCase(c)) {
                if (sb.length() > 0) sb.append('_');
                sb.append(Character.toLowerCase(c));
            } else { sb.append(c); }
        }
        return sb.toString();
    }

    /** "max_health" → "maxHealth"(1.8.9 属性字段名 camelCase)。 */
    private static String camelCase(String snake) {
        StringBuilder sb = new StringBuilder();
        boolean upper = false;
        for (char c : snake.toCharArray()) {
            if (c == '_') { upper = true; continue; }
            sb.append(upper ? Character.toUpperCase(c) : c);
            upper = false;
        }
        return sb.toString();
    }

    /** 原版实例字段读取(经映射表;1.8.9 以 public 字段代访问器:stackSize/posX/posY/posZ)。 */
    private Object fieldOf(Object target, String key) {
        try { return ReflectUtil.field(target, m(key)); }
        catch (Exception e) { throw new MosaicHandleException(key + " failed: " + e); }
    }
}
