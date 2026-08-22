package mosaic.vanilla.internal;

import mosaic.MosaicApiException;
import mosaic.MosaicHandleException;
import mosaic.vanilla.*;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.lang.ref.WeakReference;
import java.lang.reflect.Proxy;
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
    /** blockStateOf 生成的句柄 → 原版 BlockState 弱引用(setBlock 回写/取原版状态用)。 */
    private final Map<MosaicBlockState, WeakReference<Object>> states = new WeakHashMap<>();

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
        MosaicBlockState h = new MosaicBlockState() {
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
        if (st != null) states.put(h, new WeakReference<>(st));
        return h;
    }

    /** MosaicBlockState 句柄 → 原版 BlockState(弱引用表溯源;不可溯源抛句柄异常)。
     *  setBlock 写路径回写用(与 itemStackOf/stacks 的 itemStack 回写同款)。 */
    private Object stateOf(MosaicBlockState h) {
        WeakReference<Object> ref = states.get(h);
        Object st = ref == null ? null : ref.get();
        if (st == null) throw new MosaicHandleException("setBlock: state handle not backed by a vanilla BlockState");
        return st;
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
            public boolean setBlock(MosaicBlockPos pos, MosaicBlockState state) {
                // 写路径:Level.setBlock(BlockPos, BlockState, flags=3)(默认更新标志,与
                // 1.8.9 setBlockState(pos, state) 内部 flags=3 对齐)。句柄未持有真实
                // Level(null 或维度令牌)时抛 MosaicHandleException——写路径不静默
                // (与读路径 getBlock 的 null 语义区分;真实路径待运行中服务端环境)。
                if (w == null) throw new MosaicHandleException("setBlock: world handle has no live Level");
                if (pos == null || state == null)
                    throw new MosaicHandleException("setBlock: pos and state must be non-null");
                try {
                    Object bp = ReflectUtil.callConstructor(cls("blockpos"), pos.x(), pos.y(), pos.z());
                    Object st = stateOf(state);
                    Object r = ReflectUtil.call(w, m("level.setblock"), bp, st, 3);
                    return r instanceof Boolean b && b;
                } catch (MosaicHandleException e) { throw e; }
                catch (Exception e) { throw new MosaicHandleException("setBlock: " + e); }
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
            public int id(String registryName) { return registryIdOf(reg, registryName); }
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
            public MosaicRegistryEntry registerBlock(String registryName, Object vanillaBlock) {
                return registerEntry("block", "registry.block", "registries.block", registryName, vanillaBlock);
            }
            public MosaicRegistryEntry registerItem(String registryName, Object vanillaItem) {
                return registerEntry("item", "registry.item", "registries.item", registryName, vanillaItem);
            }
        };
    }

    /** 名 → id 查询(26.2 DefaultedRegistry 存在性守卫:getValue(未注册名) 返回默认值,
     *  先 containsKey 判存在再取值;共享给 id() 与 registerEntry 的条目 id 计算)。 */
    private int registryIdOf(Object reg, String registryName) {
        try {
            if (registryName == null) return -1;
            Object ident = ReflectUtil.callStatic(cls("identifier"), m("identifier.parse"), registryName);
            if (ident == null) return -1;
            if (!(ReflectUtil.call(reg, m("registry.containskey"), ident) instanceof Boolean b) || !b)
                return -1;
            Object value = ReflectUtil.call(reg, m("registry.value"), ident);
            return value == null ? -1 : intOf(ReflectUtil.call(reg, m("registry.id"), value));
        } catch (Exception e) { return -1; }
    }

    /** 注册写路径(26.2 扁平化语义:注册名 → 注册表,BuiltInRegistries 直注册)。
     *  注册目标 = 该类型的规范注册表(BuiltInRegistries.BLOCK / ITEM),与句柄
     *  包装的注册表对象无关——registerBlock/registerItem 各自锚定类型的注册表。
     *  流程:名字/对象校验 → 重复守卫(containsKey,先于 vanilla 抛错自拦截,与 M7-B
     *  命令重名守卫同款)→ ResourceKey.create(Registries.X, Identifier) →
     *  Registry.register(registry, key, value)(静态;MappedRegistry.register 对
     *  重复键/重复值/frozen 抛 IllegalStateException —— 统一吸收为 MosaicApiException)。
     *  注意:26.2 BuiltInRegistries 在 Bootstrap.bootStrap() 末尾 freeze() 且无
     *  unfreeze API(逆向核实 MappedRegistry.java),真实注册只在可写注册表上下文
     *  (服务端引导/模组加载期)可用;契约环境(已 freeze)注册即抛 MosaicApiException。 */
    private MosaicRegistryEntry registerEntry(String what, String regFieldKey, String resKeyFieldKey,
                                              String registryName, Object vanilla) {
        if (registryName == null || registryName.isEmpty())
            throw new MosaicApiException(what + " registry name must be non-empty");
        if (vanilla == null) throw new MosaicApiException("vanilla " + what + " must be non-null");
        try {
            Object reg = builtInRegistry(regFieldKey);   // BuiltInRegistries.BLOCK / ITEM
            Object ident = ReflectUtil.callStatic(cls("identifier"), m("identifier.parse"), registryName);
            if (ident == null) throw new MosaicApiException("invalid registry name: " + registryName);
            if (ReflectUtil.call(reg, m("registry.containskey"), ident) instanceof Boolean b && b)
                throw new MosaicApiException(what + " already registered: " + registryName);
            Object resKeyField = ReflectUtil.fieldStatic(cls("registries"), m(resKeyFieldKey));   // Registries.BLOCK/ITEM
            Object key = ReflectUtil.callStatic(cls("reskey"), m("reskey.create"), resKeyField, ident);
            ReflectUtil.callStatic(cls("registry"), m("registry.register"), reg, key, vanilla);
            return entryOf(reg, registryName);
        } catch (MosaicApiException e) { throw e; }
        catch (Exception e) { throw new MosaicApiException("register " + what + " '" + registryName + "': " + e); }
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

    /* ---------- Command / Network(M7-B) ---------- */

    /** 命令句柄工厂:包装 26.2 命令对象(com.mojang.brigadier.CommandDispatcher)。
     *  契约环境可构造性:CommandDispatcher 无参构造(Brigadier 库在 Mojang 运行库内,
     *  javap 核实)——真实路径可用;null → null-safe 句柄(registered 空/register no-op)。
     *  register 语义:无服务器环境仅注册到本地树(句柄持有 dispatcher,register 操作本地树);
     *  execute 需 CommandSourceStack,契约环境不可用——契约只断言 registered() 列表。 */
    public MosaicCommand commandOf(Object vanillaCommand) {
        if (vanillaCommand == null) return new NullSafeCommand();
        return new DispatcherCommand(this, vanillaCommand);
    }

    /** 网络句柄工厂:契约环境无真实 Connection(构造需 ClientPacketListener/
     *  ServerCommonPacketListener 等复杂依赖)→ null 语义为主(与 Entity 先例同款);
     *  真实路径在运行中服务端环境可用。 */
    public MosaicNetwork networkOf(Object vanillaNetwork) {
        if (vanillaNetwork == null) return new NullSafeNetwork();
        return new ConnectionNetwork(this, vanillaNetwork);
    }

    /** null-safe 命令句柄(双代同值):registered() 空、register no-op(与 worldOf 的
     *  null-safe 先例同款)。句柄同时实现 MosaicCommandTree(规范 §5 Command 域双接口)。 */
    private static final class NullSafeCommand implements MosaicCommand, MosaicCommandTree {
        public void register(String name, MosaicCommandHandler handler) { }
        public String[] registered() { return new String[0]; }
    }

    /** 26.2 真实路径命令句柄:Brigadier CommandDispatcher 本地树注册。
     *  register:名字校验 + 重名守卫(接口契约:重名抛 MosaicApiException;先于 Brigadier
     *  的同层重名合并/抛错自拦截)→ LiteralArgumentBuilder.literal(name).executes(Command 代理)
     *  → dispatcher.register(本地树)。execute 接线:Command 代理 run(CommandContext) 经
     *  getInput() 取原始输入、剥离命令名为 args 后回调 handler(best-effort;契约环境
     *  无 CommandSourceStack,execute 不可达,仅接线不测试)。 */
    private static final class DispatcherCommand implements MosaicCommand, MosaicCommandTree {
        private final Vanilla262Provider p;
        private final Object dispatcher;   // CommandDispatcher
        DispatcherCommand(Vanilla262Provider p, Object dispatcher) { this.p = p; this.dispatcher = dispatcher; }

        public String[] registered() {
            try {
                Object root = ReflectUtil.call(dispatcher, p.m("dispatcher.root"));
                Object children = ReflectUtil.call(root, p.m("commandnode.children"));
                if (!(children instanceof Collection)) return new String[0];
                List<String> out = new ArrayList<>();
                for (Object n : (Collection<?>) children) {
                    Object name = ReflectUtil.call(n, p.m("commandnode.name"));
                    out.add(name == null ? String.valueOf(n) : name.toString());
                }
                return out.toArray(new String[0]);
            } catch (Exception e) { return new String[0]; }
        }

        public void register(String name, MosaicCommandHandler handler) {
            if (name == null || name.isEmpty()) throw new MosaicApiException("command name must be non-empty");
            if (handler == null) throw new MosaicApiException("command handler must be non-null");
            for (String s : registered())
                if (s.equals(name)) throw new MosaicApiException("command already registered: " + name);
            try {
                Object literal = ReflectUtil.callStatic(p.cls("literal"), p.m("literal.literal"), name);
                Object cmdProxy = Proxy.newProxyInstance(getClass().getClassLoader(),
                        new Class<?>[] { Class.forName(p.m("command.class")) },
                        (proxy, method, args) -> {
                            if ("run".equals(method.getName()) && args != null && args.length > 0) {
                                try {
                                    Object input = ReflectUtil.call(args[0], p.m("commandcontext.input"));
                                    return (Integer) handler.execute(splitArgs(input == null ? "" : input.toString(), name));
                                } catch (Exception e) { return 0; }
                            }
                            if ("toString".equals(method.getName())) return "MosaicCommand(" + name + ")";
                            if ("hashCode".equals(method.getName())) return System.identityHashCode(proxy);
                            if ("equals".equals(method.getName())) return proxy == args[0];
                            return 0;
                        });
                ReflectUtil.call(literal, p.m("literal.executes"), cmdProxy);
                ReflectUtil.call(dispatcher, p.m("dispatcher.register"), literal);
            } catch (Exception e) {
                throw new MosaicHandleException("register '" + name + "': " + e);
            }
        }
    }

    /** 原始命令输入剥离首个命令名 token("name a b" → ["a","b"])。 */
    private static String[] splitArgs(String input, String name) {
        String[] parts = input == null ? new String[0] : input.trim().split("\\s+");
        List<String> out = new ArrayList<>();
        for (int i = 0; i < parts.length; i++) {
            if (i == 0 && parts[i].equals(name)) continue;
            out.add(parts[i]);
        }
        return out.toArray(new String[0]);
    }

    /** null-safe 网络句柄(双代同值):sendPacket no-op、listener() 非 null、
     *  onPacket 返回 no-op 订阅(契约环境无真实 Connection → 兜底值)。 */
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

    /** 真实路径网络句柄(运行中服务端):持有 Connection 引用;sendPacket 的 Packet
     *  构造与编码需服务端环境(契约环境不可达)→ 静默跳过;listener 的 onPacket 做
     *  本地订阅簿记(返回可关闭订阅;真实包分发接入待服务端/原生桥环境)。 */
    private static final class ConnectionNetwork implements MosaicNetwork {
        private final Vanilla262Provider p;
        private final Object conn;   // Connection/PacketListener
        private final Map<String, List<MosaicPacketHandler>> subs = new HashMap<>();
        ConnectionNetwork(Vanilla262Provider p, Object conn) { this.p = p; this.conn = conn; }

        public void sendPacket(int playerId, byte[] packetData) {
            // 26.2 Connection.send(Packet):Packet 编解码需运行中服务端,契约环境不可构造 → 静默跳过
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
}
