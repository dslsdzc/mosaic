package mosaic.vanilla.internal;

import mosaic.MosaicHandleException;
import mosaic.vanilla.*;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.lang.ref.WeakReference;
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
        return new MosaicBlockState() {
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
            public int id(String registryName) {
                try {
                    if (registryName == null) return -1;
                    Object rl = ReflectUtil.callConstructor(cls("resloc"), registryName);
                    if (rl == null) return -1;
                    // 1.8.9 blockRegistry 为 DefaultedByKey:未注册名返回默认值(air),
                    // 须先 containsKey 存在性守卫(与 26.2 DefaultedRegistry 同款)
                    if (!(ReflectUtil.call(reg, m("registry.containskey"), rl) instanceof Boolean b) || !b)
                        return -1;
                    Object value = ReflectUtil.call(reg, m("registry.value"), rl);
                    return value == null ? -1 : intOf(ReflectUtil.call(reg, m("registry.id"), value));
                } catch (Exception e) { return -1; }
            }
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

    /* ---------- 名称工具 ---------- */

    private static String stripNamespace(String name) {
        int i = name == null ? -1 : name.indexOf(':');
        return i >= 0 ? name.substring(i + 1) : name;
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
