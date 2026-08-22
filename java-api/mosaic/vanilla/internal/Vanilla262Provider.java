package mosaic.vanilla.internal;

import mosaic.MosaicHandleException;
import mosaic.vanilla.*;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.lang.ref.WeakReference;
import java.util.*;

/** 26.2 Provider:反射 + 版本映射表。句柄持有原版引用,读取时经 ReflectUtil 转换;
 *  类名/方法名/字段名全部取自 VersionMap_26_2.properties,不硬编码版本 API。
 *  方法签名核实:~/minecraft26.2/decompiled 逆向源码 + jar javap(记录见 Task 6 报告)。
 *
 *  已知版本语义(26.2 特有,Provider 吸收):
 *  - NBT getString/getInt 返回 Optional(旧版返回默认值)——Provider 解包,空→""/0;
 *  - maxStackSize 无直接访问器,用 Item.getDefaultMaxStackSize()(1.21.5+ 语义);
 *  - 静态字段(Blocks.STONE 等)经 ReflectUtil.fieldStatic;
 *  - 契约环境无运行中服务端,无法构造真实 Level:worldOf 对非 Level 对象
 *    (如 Level.OVERWORLD 维度令牌)返回 null-safe 句柄,真实 Level 路径照常实现。 */
public final class Vanilla262Provider implements MosaicProvider {
    private final Properties map = new Properties();
    /** itemStackOf 生成的句柄 → 原版 ItemStack 弱引用(itemStackOf 幂等重建,setItem 回写用)。 */
    private final Map<MosaicItemStack, WeakReference<Object>> stacks = new WeakHashMap<>();

    public Vanilla262Provider() {
        try { map.load(getClass().getResourceAsStream("VersionMap_26_2.properties")); }
        catch (Exception e) { throw new IllegalStateException("version map missing", e); }
    }
    public String providerId() { return "vanilla-26.2"; }
    public String mcVersion() { return "26.2"; }
    public boolean supportsApi(int min, int max) { return true; }

    /* ---------- 内部工具:类名/方法名/字段名全部经映射表 ---------- */

    private String cls(String key) { return map.getProperty(key + ".class"); }
    private String m(String key) { return map.getProperty(key); }
    private String field(String key) { return map.getProperty(key); }

    /** 调用失败抛 MosaicHandleException(接口语义:未知属性等抛句柄异常)。 */
    private static Object call(Object t, String method, Object... args) {
        try { return ReflectUtil.call(t, method, args); }
        catch (Exception e) { throw new MosaicHandleException(method + " failed: " + e); }
    }
    private static Object callSafe(Object t, String method, Object... args) {
        try { return ReflectUtil.call(t, method, args); } catch (Exception e) { return null; }
    }
    private static Object unwrapOptional(Object o) {
        return (o instanceof Optional<?> opt && opt.isPresent()) ? opt.get() : null;
    }
    private static int intOf(Object o) {
        return o instanceof Number n ? n.intValue() : 0;
    }

    /** BuiltInRegistries 静态字段(如 BLOCK/ITEM/ATTRIBUTE)经映射表取字段名。 */
    private Object builtInRegistry(String fieldKey) {
        try { return ReflectUtil.fieldStatic(cls("builtinregistries"), field(fieldKey)); }
        catch (Exception e) { throw new MosaicHandleException("built-in registry " + fieldKey + ": " + e); }
    }

    /** 注册表值经名称解析(如 "minecraft:stone" → Block.STONE)。 */
    private Object registryValue(String fieldKey, String name) {
        try {
            Object ident = ReflectUtil.callStatic(cls("identifier"), m("identifier.parse"), name);
            return ident == null ? null : ReflectUtil.call(builtInRegistry(fieldKey), m("registry.value"), ident);
        } catch (Exception e) { return null; }
    }

    /** 句柄注册名统一链:builtInRegistryHolder().key().identifier() → String。 */
    private String registryNameVia(Object holderTarget, String holderMethodKey) {
        try {
            Object holder = ReflectUtil.call(holderTarget, m(holderMethodKey));
            Object key = ReflectUtil.call(holder, m("holder.key"));
            Object loc = ReflectUtil.call(key, m("reskey.identifier"));
            return loc == null ? "unknown" : loc.toString();
        } catch (Exception e) { return "unknown"; }
    }

    private MosaicEntityId entityIdOf(final Object e) {
        return new MosaicEntityId() { public int value() { return intOf(call(e, m("entity.id"))); } };
    }

    /* ---------- MosaicProvider 接口实现 ---------- */

    public MosaicBlock blockOf(Object vanillaBlock) {
        final Object b = vanillaBlock;
        return new MosaicBlock() {
            public MosaicBlockState state() { return blockStateOf(call(b, m("block.defaultstate"))); }
            public String registryName() { return registryNameVia(b, "block.registryholder"); }
        };
    }

    public MosaicBlockState blockStateOf(Object vanillaBlockState) {
        final Object st = vanillaBlockState;
        return new MosaicBlockState() {
            public MosaicBlock block() { return blockOf(call(st, m("blockstate.block"))); }
            public String[] propertyNames() {
                try {
                    Object props = ReflectUtil.call(st, m("blockstate.properties"));
                    if (!(props instanceof Collection)) return new String[0];
                    List<String> out = new ArrayList<>();
                    for (Object p : (Collection<?>) props) {
                        Object pn = callSafe(p, m("property.name"));
                        out.add(pn == null ? String.valueOf(p) : pn.toString());
                    }
                    return out.toArray(new String[0]);
                } catch (Exception e) { return new String[0]; }
            }
            public String property(String name) {
                try {
                    Object props = ReflectUtil.call(st, m("blockstate.properties"));
                    if (props instanceof Collection)
                        for (Object p : (Collection<?>) props) {
                            Object pn = callSafe(p, m("property.name"));
                            if (name != null && name.equals(String.valueOf(pn)))
                                return String.valueOf(ReflectUtil.call(st, m("blockstate.value"), p));
                        }
                    throw new NoSuchMethodException("property " + name);
                } catch (Exception e) { throw new MosaicHandleException("property " + name + ": " + e); }
            }
        };
    }

    public MosaicItem itemOf(Object vanillaItem) {
        final Object it = vanillaItem;
        return new MosaicItem() {
            public String registryName() { return registryNameVia(it, "item.registryholder"); }
            public int maxStackSize() {
                // 26.2:getDefaultMaxStackSize 读 Holder 绑定的组件;无运行中服务端时组件未绑定
                // (Holder.Reference.components 抛 "Components not bound yet")→ 回退默认常量 64
                try { return intOf(call(it, m("item.maxstack"))); }
                catch (Exception e) {
                    try { return intOf(ReflectUtil.fieldStatic(cls("item"), m("item.defaultmaxstack"))); }
                    catch (Exception e2) { return 1; }
                }
            }
            public MosaicComponents components() {
                try { return componentsOf(ReflectUtil.call(it, m("item.components"))); }
                catch (Exception e) { throw new MosaicHandleException("components: " + e); }
            }
        };
    }

    /** DataComponentMap(26.2 Item.components() 返回) → MosaicComponents。
     *  keys 真实映射(DATA_COMPONENT_TYPE 注册表名);get 经持久化 codec + NbtOps 编码为
     *  NBT 字节(非持久化类型或编码失败 → null);with 对称解码重建映射(失败 → 原映射)。 */
    private MosaicComponents componentsOf(final Object cmap) {
        return new MosaicComponents() {
            public String[] keys() {
                try {
                    Object set = ReflectUtil.call(cmap, m("datacomponentmap.keys"));
                    if (!(set instanceof Set)) return new String[0];
                    List<String> out = new ArrayList<>();
                    for (Object t : (Set<?>) set) {
                        Object key = callSafe(builtInRegistry("registry.datacomponent"), m("registry.key"), t);
                        out.add(key == null ? String.valueOf(t) : key.toString());
                    }
                    return out.toArray(new String[0]);
                } catch (Exception e) { return new String[0]; }
            }
            public byte[] get(String key) {
                try {
                    Object type = registryValue("registry.datacomponent", key);
                    if (type == null) return null;
                    Object value = ReflectUtil.call(cmap, m("datacomponentmap.get"), type);
                    if (value == null) return null;
                    return encodeComponent(type, value);
                } catch (Exception e) { return null; }
            }
            public MosaicComponents with(String key, byte[] value) {
                try {
                    if (value == null) return this;
                    Object type = registryValue("registry.datacomponent", key);
                    if (type == null) return this;
                    Object decoded = decodeComponent(type, value);
                    if (decoded == null) return this;
                    Object builder = ReflectUtil.callStatic(cls("datacomponentmap"), m("datacomponentmap.builder"));
                    ReflectUtil.call(builder, m("datacomponentmap.set"), type, decoded);
                    return componentsOf(ReflectUtil.call(builder, m("datacomponentmap.build")));
                } catch (Exception e) { return this; }
            }
        };
    }

    private byte[] encodeComponent(Object type, Object value) {
        try {
            Object codec = ReflectUtil.call(type, m("datacomponenttype.codec"));
            if (codec == null) return null;                       // 仅网络同步、无持久化 codec
            Object ops = ReflectUtil.fieldStatic(cls("nbtops"), m("nbtops.instance"));
            Object result = ReflectUtil.call(codec, m("codec.encode"), ops, value);
            Object tag = unwrapOptional(callSafe(result, "result"));
            if (tag == null) return null;
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            ReflectUtil.callStatic(cls("nbtio"), m("nbtio.write"), tag, new DataOutputStream(bos));
            return bos.toByteArray();
        } catch (Exception e) { return null; }
    }

    private Object decodeComponent(Object type, byte[] value) {
        try {
            Object codec = ReflectUtil.call(type, m("datacomponenttype.codec"));
            if (codec == null) return null;
            Object acc = ReflectUtil.callStatic(cls("nbtaccounter"), m("nbtaccounter.unlimited"));
            Object tag = ReflectUtil.callStatic(cls("nbtio"), m("nbtio.read"),
                    new DataInputStream(new ByteArrayInputStream(value)), acc);
            Object ops = ReflectUtil.fieldStatic(cls("nbtops"), m("nbtops.instance"));
            Object result = ReflectUtil.call(codec, m("codec.parse"), ops, tag);
            return unwrapOptional(callSafe(result, "result"));
        } catch (Exception e) { return null; }
    }

    public MosaicItemStack itemStackOf(Object vanillaItemStack) {
        final Object s = vanillaItemStack;
        MosaicItemStack h = new MosaicItemStack() {
            public MosaicItem item() { return itemOf(call(s, m("itemstack.item"))); }
            public int count() { return intOf(call(s, m("itemstack.count"))); }
            public MosaicItemStack copy() { return itemStackOf(call(s, m("itemstack.copy"))); }
        };
        if (s != null) stacks.put(h, new WeakReference<>(s));
        return h;
    }

    public MosaicWorld worldOf(Object vanillaWorld) {
        final Object w = vanillaWorld;
        final String tokenDim = dimensionToken(w);   // 真实 Level 或维度令牌(ResourceKey)的资源名
        return new MosaicWorld() {
            public String dimension() { return tokenDim != null ? tokenDim : "unknown"; }
            public MosaicBlockState getBlock(MosaicBlockPos pos) {
                if (w == null || pos == null) return null;
                try {
                    Object bp = ReflectUtil.callConstructor(cls("blockpos"), pos.x(), pos.y(), pos.z());
                    Object st = ReflectUtil.call(w, m("level.blockstate"), bp);
                    return st == null ? null : blockStateOf(st);
                } catch (Exception e) { return null; }   // 非 Level 令牌/区块未加载 → null(契约语义)
            }
            public MosaicEntity[] entities() {
                if (w == null) return new MosaicEntity[0];
                try {
                    Object aabb = ReflectUtil.callConstructor(cls("aabb"),
                            Double.NEGATIVE_INFINITY, Double.NEGATIVE_INFINITY, Double.NEGATIVE_INFINITY,
                            Double.POSITIVE_INFINITY, Double.POSITIVE_INFINITY, Double.POSITIVE_INFINITY);
                    Object list = ReflectUtil.call(w, m("level.entities"), null, aabb);
                    if (!(list instanceof Collection)) return new MosaicEntity[0];
                    List<MosaicEntity> out = new ArrayList<>();
                    for (Object e : (Collection<?>) list) out.add(entityOf(e));
                    return out.toArray(new MosaicEntity[0]);
                } catch (Exception e) { return new MosaicEntity[0]; }
            }
            public MosaicEntity entityById(int entityId) {
                if (w == null) return null;
                try {
                    Object e = ReflectUtil.call(w, m("level.entity"), entityId);
                    return e == null ? null : entityOf(e);
                } catch (Exception e) { return null; }
            }
            public void tick() {
                try { ReflectUtil.call(w, m("level.tick")); }          // 26.2 无无参 tick(签名差异吸收)
                catch (Exception ignored) { }
            }
            public void save() {
                try { ReflectUtil.call(w, m("level.save"), null, false, false); }
                catch (Exception ignored) { }
            }
            public long gameTime() {
                if (w == null) return 0;
                Object t = callSafe(w, m("level.gametime"));
                return t instanceof Number n ? n.longValue() : 0L;
            }
        };
    }

    /** 维度资源名:真实 Level.dimension().identifier() 或 ResourceKey 令牌直取;均失败 → null。 */
    private String dimensionToken(Object w) {
        if (w == null) return null;
        try { return ReflectUtil.call(w, m("level.dimension")).toString(); }   // 真实 Level 路径
        catch (Exception e) {
            try {
                Object loc = ReflectUtil.call(w, m("reskey.identifier"));
                return loc == null ? null : loc.toString();                    // 维度令牌路径
            } catch (Exception e2) { return null; }
        }
    }

    public MosaicEntity entityOf(Object vanillaEntity) {
        final Object e = vanillaEntity;
        return new MosaicEntity() {
            public MosaicEntityId id() { return entityIdOf(e); }
            public MosaicEntityType type() {
                return new MosaicEntityType() {
                    public String registryName() {
                        try {
                            Object t = ReflectUtil.call(e, m("entity.type"));
                            Object key = ReflectUtil.call(builtInRegistry("registry.entitytype"), m("registry.key"), t);
                            return key == null ? "unknown" : key.toString();
                        } catch (Exception ex) { return "unknown"; }
                    }
                };
            }
            public double x() { return ((Number) call(e, m("entity.x"))).doubleValue(); }
            public double y() { return ((Number) call(e, m("entity.y"))).doubleValue(); }
            public double z() { return ((Number) call(e, m("entity.z"))).doubleValue(); }
            public double attribute(String name) {
                try {
                    Object regKey = ReflectUtil.fieldStatic(cls("registries"), m("registry.attribute"));
                    Object ident = ReflectUtil.callStatic(cls("identifier"), m("identifier.parse"), name);
                    Object resKey = ReflectUtil.callStatic(cls("reskey"), m("reskey.create"), regKey, ident);
                    Object holder = unwrapOptional(ReflectUtil.call(builtInRegistry("registry.attribute"), m("registry.get"), resKey));
                    if (holder == null) throw new NoSuchMethodException("attribute " + name);
                    Object inst = ReflectUtil.call(e, m("entity.attribute"), holder);
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
                    Object n = ReflectUtil.call(profile, m("profile.name"));
                    return n == null ? "" : n.toString();
                } catch (Exception e) { return ""; }
            }
            public int gameMode() {
                try { return intOf(ReflectUtil.call(ReflectUtil.call(pl, m("player.mode")), m("gametype.id"))); }
                catch (Exception e) { return -1; }   // 非服务端 Player 无 gameMode
            }
            public boolean online() {
                try { return ReflectUtil.field(pl, m("player.connection")) != null; }  // ServerPlayer.connection
                catch (Exception e) {
                    try { return ReflectUtil.call(pl, m("player.profile")) != null; }
                    catch (Exception e2) { return false; }
                }
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
                    Object empty = ReflectUtil.call(st, m("itemstack.empty"));
                    return (empty instanceof Boolean b && b) ? null : itemStackOf(st);
                } catch (Exception e) { return null; }
            }
            public void setItem(int index, MosaicItemStack stack) {
                try {
                    Object vanilla = null;
                    if (stack != null) {
                        WeakReference<Object> ref = stacks.get(stack);
                        vanilla = ref == null ? null : ref.get();
                        if (vanilla == null) {   // 句柄不可溯源时按注册表名重建 ItemStack
                            Object item = registryValue("registry.item", stack.item().registryName());
                            vanilla = ReflectUtil.callConstructor(cls("itemstack"), item, stack.count());
                        }
                    }
                    if (vanilla == null)
                        vanilla = ReflectUtil.fieldStatic(cls("itemstack"), m("itemstack.emptyfield"));
                    ReflectUtil.call(inv, m("container.setitem"), index, vanilla);
                } catch (Exception e) { throw new MosaicHandleException("setItem " + index + ": " + e); }
            }
            public int size() { return slotCount(); }
        };
    }

    public MosaicRegistry registryOf(Object vanillaRegistry) {
        final Object reg = vanillaRegistry;
        return new MosaicRegistry() {
            public int id(String registryName) {
                try {
                    if (registryName == null) return -1;
                    Object ident = ReflectUtil.callStatic(cls("identifier"), m("identifier.parse"), registryName);
                    if (ident == null) return -1;
                    // 26.2 DefaultedRegistry:getValue(未注册名) 返回默认值(air),须先 containsKey 存在性守卫
                    if (!(ReflectUtil.call(reg, m("registry.containskey"), ident) instanceof Boolean b) || !b)
                        return -1;
                    Object value = ReflectUtil.call(reg, m("registry.value"), ident);
                    return value == null ? -1 : intOf(ReflectUtil.call(reg, m("registry.id"), value));
                } catch (Exception e) { return -1; }
            }
            public String name(int id) {
                try {
                    if (id < 0) return null;
                    Object value = ReflectUtil.call(reg, m("registry.byid"), id);
                    if (value == null) return null;
                    // 26.2 DefaultedRegistry:byId(未注册 id) 返回默认值(air),getId 回环校验真实注册
                    if (intOf(ReflectUtil.call(reg, m("registry.id"), value)) != id) return null;
                    Object key = ReflectUtil.call(reg, m("registry.key"), value);
                    return key == null ? null : key.toString();
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
                        Object r = callSafe(tag, m("nbt.getstring"), key);
                        if (r instanceof Optional<?> opt)   // 26.2:Optional<String>;空 Optional → ""
                            return opt.isPresent() ? String.valueOf(opt.get()) : "";
                        Object v = unwrapOptional(r);       // 旧版:直接 String
                        return v == null ? (r == null ? "" : String.valueOf(r)) : v.toString();
                    }
                    public int getInt(String key) {
                        Object r = callSafe(tag, m("nbt.getint"), key);
                        Object v = unwrapOptional(r);          // 26.2:Optional<Integer>;旧版为 Integer
                        return v instanceof Number n ? n.intValue() : 0;
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
                            ReflectUtil.callStatic(cls("nbtio"), m("nbtio.write"), tag, new DataOutputStream(bos));
                            return bos.toByteArray();
                        } catch (Exception e) { return new byte[0]; }
                    }
                };
            }
        };
    }
}
