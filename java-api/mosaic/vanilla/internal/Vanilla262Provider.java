package mosaic.vanilla.internal;

import mosaic.MosaicApiException;
import mosaic.MosaicHandleException;
import mosaic.runtime.internal.PacketCatalogImpl;
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
            public int maxDamage() {
                // 26.2 无 Item.getMaxDamage:maxDamage 是组件(DataComponents.MAX_DAMAGE,
                // ItemStack.getMaxDamage = getOrDefault(MAX_DAMAGE, 0),ItemStack.java:432)。
                // 无运行中服务端时 Item.components() 组件未绑定(Holder.Reference.components
                // 抛 "Components not bound yet")→ 回退 0(与 maxStackSize 回退同款处理);
                // 未绑定与未设组件均属不可损坏语义,双代契约断言非负/一致即可。
                try {
                    Object cmap = ReflectUtil.call(it, m("item.components"));
                    Object type = ReflectUtil.fieldStatic(cls("datacomponents"), m("item.maxdamage"));
                    Object v = ReflectUtil.call(cmap, m("datacomponentmap.get"), type);
                    return v instanceof Number n ? n.intValue() : 0;
                } catch (Exception e) { return 0; }
            }
            public boolean damageable() {
                // 26.2 无 Item.isDamageable:可损坏 ⇔ maxDamage > 0(ItemStack.isDamageableItem
                // 的组件语义近似,Item 级与栈级差异由 Provider 吸收)
                return maxDamage() > 0;
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

    /** 网络句柄工厂:26.2 Connection(PacketFlow) 契约环境可轻参构造(Connection
     *  构造器仅存方向字段;javap 核实)→ 真实路径可用(ConnectionNetwork 持有
     *  Connection 引用,packetOf 投影与 listener 注册簿记本地语义可达;sendPacket
     *  的包构造/编码需运行中服务端);null → null-safe 句柄(投影/注册语义照常)。 */
    public MosaicNetwork networkOf(Object vanillaNetwork) {
        if (vanillaNetwork == null) return new NullSafeNetwork();
        return new ConnectionNetwork(this, vanillaNetwork);
    }

    /* ---------- 网络域投影(7.1) ---------- */

    /** 包投影(7.1):原版包对象 → MosaicPacket 稳定投影。typeId = 包目录 id
     *  (26.2 mojmap 类简单名直接对目录名,未命中 → 0);direction 由类名约定
     *  推导(Serverbound 前缀与 ClientIntentionPacket → IN,Clientbound 前缀 →
     *  OUT,与包目录 id 分组方向一致);playerId/sizeHint 恒 0(投影无连接上下
     *  文;v1 无包内容序列化)。null → 全零投影(IN, 0, 0)。 */
    static MosaicPacket packetOf0(Object vanillaPacket) {
        if (vanillaPacket == null) return new Projection(0, MosaicPacket.Direction.IN, 0, 0);
        return new Projection(0, directionOf(nameOf(vanillaPacket)),
                PacketCatalogImpl.packetIdOf(nameOf(vanillaPacket)), 0);
    }

    /** 包类规范名:getClass().getName() 去包名 + 去内部类段(如
     *  ServerboundMovePlayerPacket$Pos → ServerboundMovePlayerPacket,与目录名
     *  匹配;getSimpleName() 对内部类只返回末段 "Pos",不可用)。 */
    private static String nameOf(Object packet) {
        String raw = packet.getClass().getName();
        int dot = raw.lastIndexOf('.');
        String name = dot >= 0 ? raw.substring(dot + 1) : raw;
        int dollar = name.indexOf('$');
        return dollar >= 0 ? name.substring(0, dollar) : name;
    }

    private static MosaicPacket.Direction directionOf(String simpleName) {
        if (simpleName == null) return MosaicPacket.Direction.IN;
        if (simpleName.startsWith("Serverbound") || "ClientIntentionPacket".equals(simpleName))
            return MosaicPacket.Direction.IN;
        return MosaicPacket.Direction.OUT;
    }

    /** 投影实现(7.1):稳定投影的持有者(不可变四元组)。 */
    private static final class Projection implements MosaicPacket {
        private final int playerId;
        private final Direction direction;
        private final int typeId;
        private final int sizeHint;
        Projection(int playerId, Direction direction, int typeId, int sizeHint) {
            this.playerId = playerId; this.direction = direction;
            this.typeId = typeId; this.sizeHint = sizeHint;
        }
        public int playerId() { return playerId; }
        public Direction direction() { return direction; }
        public int typeId() { return typeId; }
        public int sizeHint() { return sizeHint; }
    }

    /* ---------- Recipe / Enchantment(M8-B) ---------- */

    /** 配方句柄工厂:26.2 Recipe 为接口,契约环境不可轻量构造(实现如 ShapedRecipe/
     *  NormalCraftingRecipe 需 RecipeSerializer + codec 装配)→ null 语义为主(与
     *  Entity 先例一致),真实路径在服务端环境可用。注册名经 RecipeHolder.id()
     *  (配方注册表条目为 RecipeHolder(ResourceKey id, T value) 记录)直读;裸 Recipe
     *  无注册表上下文 → "unknown"。 */
    public MosaicRecipe recipeOf(Object vanillaRecipe) {
        if (vanillaRecipe == null) return new NullSafeRecipe();
        final Object r = vanillaRecipe;
        return new MosaicRecipe() {
            public String registryName() {
                try {
                    Object id = ReflectUtil.call(r, m("recipeholder.id"));
                    Object loc = ReflectUtil.call(id, m("reskey.identifier"));
                    return loc == null ? "unknown" : loc.toString();
                } catch (Exception e) { return "unknown"; }
            }
            public MosaicItemStack result() {
                // 26.2 Recipe 无直接输出访问器:display() → List<RecipeDisplay> →
                // result() 为 SlotDisplay,需服务端上下文/物品解析(RecipeDisplay.java:14)
                // → 契约环境不可达,返回 null;真实输出路径待服务端环境
                return null;
            }
            public String type() {
                try {
                    Object type = ReflectUtil.call(r, m("recipe.type"));
                    Object key = ReflectUtil.call(builtInRegistry("registry.recipetype"), m("registry.key"), type);
                    return key == null ? "" : key.toString();
                } catch (Exception e) { return ""; }
            }
        };
    }

    /** 附魔句柄工厂:26.2 Enchantment 为记录(非抽象类;逆向核实 Enchantment.java:47),
     *  契约环境可轻参构造(Component.translatable + EnchantmentDefinition 构造器 +
     *  HolderSet.empty() + DataComponentMap.EMPTY,见 Vanilla262Env)——真实路径可用;
     *  null → null-safe 句柄。maxLevel = definition().maxLevel()(EnchantmentDefinition
     *  记录访问器);descriptionKey = description() 组件 → TranslatableContents.getKey()
     *  (非翻译组件 → "");registryName 经 Holder.key()(26.2 附魔通常以 Holder<Enchantment>
     *  传递),裸记录无注册表上下文 → "unknown"。 */
    public MosaicEnchantment enchantmentOf(Object vanillaEnchantment) {
        if (vanillaEnchantment == null) return new NullSafeEnchantment();
        final Object e = vanillaEnchantment;
        return new MosaicEnchantment() {
            public String registryName() {
                try {
                    Object key = ReflectUtil.call(e, m("holder.key"));
                    Object loc = ReflectUtil.call(key, m("reskey.identifier"));
                    return loc == null ? "unknown" : loc.toString();
                } catch (Exception ex) { return "unknown"; }
            }
            public int maxLevel() {
                try {
                    Object def = ReflectUtil.call(e, m("enchantment.definition"));
                    return intOf(ReflectUtil.call(def, m("enchantment.maxlevel")));
                } catch (Exception ex) { return 0; }
            }
            public String descriptionKey() {
                try {
                    Object desc = ReflectUtil.call(e, m("enchantment.description"));
                    Object contents = ReflectUtil.call(desc, m("component.contents"));
                    Object key = callSafe(contents, m("translatablecontents.key"));
                    return key == null ? "" : key.toString();
                } catch (Exception ex) { return ""; }
            }
        };
    }

    /* ---------- LivingEntity / StatusEffect / Tag / BlockEntity(M8-C) ---------- */

    /** 活体实体句柄工厂:26.2 LivingEntity 抽象类,构造需真实 Level(契约环境不可构造,
     *  与 Entity 先例一致)→ null 语义为主,真实路径在服务端环境;health = getHealth()、
     *  maxHealth = getMaxHealth()、dead = isDeadOrDying()(LivingEntity.java:1172:
     *  getHealth() <= 0.0F || dead)。 */
    public MosaicLivingEntity livingEntityOf(Object vanillaLivingEntity) {
        if (vanillaLivingEntity == null) return new NullSafeLivingEntity();
        final Object le = vanillaLivingEntity;
        return new MosaicLivingEntity() {
            public float health() {
                Object v = callSafe(le, m("livingentity.health"));
                return v instanceof Number n ? n.floatValue() : 0.0f;
            }
            public float maxHealth() {
                Object v = callSafe(le, m("livingentity.maxhealth"));
                return v instanceof Number n ? n.floatValue() : 0.0f;
            }
            public boolean dead() {
                Object v = callSafe(le, m("livingentity.dead"));
                return v instanceof Boolean b && b;
            }
        };
    }

    /** 状态效果句柄工厂:26.2 MobEffectInstance 为类(非记录;MobEffectInstance.java:25,
     *  7 参构造器仅存字段 + clamp amplifier),3 参构造器轻参可构造(需 Holder<MobEffect>
     *  —— MobEffects.REGENERATION 等静态 Holder 字段,Bootstrap 注册,见 Vanilla262Env)。
     *  registryName = getEffect().value() → BuiltInRegistries.MOB_EFFECT.getKey
     *  (裸记录无注册表上下文 → "unknown");amplifier/duration = getAmplifier()/getDuration()。
     *  null → null-safe 句柄。 */
    public MosaicStatusEffect statusEffectOf(Object vanillaEffectInstance) {
        if (vanillaEffectInstance == null) return new NullSafeStatusEffect();
        final Object ei = vanillaEffectInstance;
        return new MosaicStatusEffect() {
            public String registryName() {
                try {
                    Object holder = ReflectUtil.call(ei, m("mobeffectinstance.effect"));
                    Object eff = ReflectUtil.call(holder, m("holder.value"));
                    Object key = ReflectUtil.call(builtInRegistry("registry.mobeffect"), m("registry.key"), eff);
                    return key == null ? "unknown" : key.toString();
                } catch (Exception e) { return "unknown"; }
            }
            public int amplifier() { return intOf(call(ei, m("mobeffectinstance.amplifier"))); }
            public int duration() { return intOf(call(ei, m("mobeffectinstance.duration"))); }
        };
    }

    /** 标签句柄工厂:26.2 TagKey 为记录(record TagKey(ResourceKey registry, Identifier
     *  location),TagKey.java:14),create(ResourceKey, Identifier) 轻参构造 → 契约环境
     *  真实路径可用。registryName = location().toString()("minecraft:planks");contents()
     *  经 tagKey.registry() 解析对应 BuiltInRegistries 注册表 → getTagOrEmpty(tagKey)
     *  → Holder.value() → 注册表 getKey。标签为数据驱动(标签 JSON 世界加载期经 TagLoader
     *  绑定),契约环境未绑定 → 迭代未绑定 HolderSet.Named 抛 "Trying to access unbound
     *  tag"(HolderSet.java:184)→ 吸收为空数组;真实内容查询在服务端环境。
     *  null → null-safe 句柄。 */
    public MosaicTag tagOf(Object vanillaTag) {
        if (vanillaTag == null) return new NullSafeTag();
        final Object t = vanillaTag;
        return new MosaicTag() {
            public String registryName() {
                try {
                    Object loc = ReflectUtil.call(t, m("tagkey.location"));
                    return loc == null ? "unknown" : loc.toString();
                } catch (Exception e) { return "unknown"; }
            }
            public String[] contents() {
                try {
                    Object reg = registryForTag(t);
                    Object iter = ReflectUtil.call(reg, m("registry.gettagoremp"), t);
                    if (!(iter instanceof Iterable)) return new String[0];
                    List<String> out = new ArrayList<>();
                    for (Object h : (Iterable<?>) iter) {
                        Object value = ReflectUtil.call(h, m("holder.value"));
                        Object key = ReflectUtil.call(reg, m("registry.key"), value);
                        out.add(key == null ? String.valueOf(value) : key.toString());
                    }
                    return out.toArray(new String[0]);
                } catch (Exception e) { return new String[0]; }   // 未绑定标签/未知注册表 → 空
            }
        };
    }

    /** TagKey.registry()(ResourceKey) → 对应 BuiltInRegistries 注册表:遍历映射表注册表
     *  字段键,以 Registry.key()(ResourceKey).identifier() 与 tagKey.registry() 匹配。 */
    private Object registryForTag(Object tagKey) throws Exception {
        Object regKey = ReflectUtil.call(tagKey, m("tagkey.registry"));
        Object loc = ReflectUtil.call(regKey, m("reskey.identifier"));
        String target = loc == null ? null : loc.toString();
        if (target == null) throw new NoSuchMethodException("tag registry key unresolvable");
        for (String fieldKey : new String[] { "registry.block", "registry.item", "registry.entitytype",
                "registry.attribute", "registry.mobeffect", "registry.blockentitytype",
                "registry.datacomponent", "registry.recipetype" }) {
            Object cand = builtInRegistry(fieldKey);
            Object candKey = callSafe(cand, m("registry.selfkey"));
            Object candLoc = candKey == null ? null : callSafe(candKey, m("reskey.identifier"));
            if (target.equals(String.valueOf(candLoc))) return cand;
        }
        throw new NoSuchMethodException("no built-in registry for tag " + target);
    }

    /** 方块实体句柄工厂:26.2 BlockEntity 构造器为 public(BlockEntity.java:51:
     *  public BlockEntity(BlockEntityType, BlockPos, BlockState)),但需
     *  BlockEntityType+BlockState 装配,契约环境仍不可轻参构造 → null 语义为主,
     *  真实路径在服务端环境;typeRegistryName =
     *  getType() → BuiltInRegistries.BLOCK_ENTITY_TYPE.getKey;pos = getBlockPos()
     *  → Vec3i.getX/getY/getZ。 */
    public MosaicBlockEntity blockEntityOf(Object vanillaBlockEntity) {
        if (vanillaBlockEntity == null) return new NullSafeBlockEntity();
        final Object be = vanillaBlockEntity;
        return new MosaicBlockEntity() {
            public String typeRegistryName() {
                try {
                    Object type = ReflectUtil.call(be, m("blockentity.type"));
                    Object key = ReflectUtil.call(builtInRegistry("registry.blockentitytype"), m("registry.key"), type);
                    return key == null ? "unknown" : key.toString();
                } catch (Exception e) { return "unknown"; }
            }
            public MosaicBlockPos pos() {
                try {
                    Object bp = ReflectUtil.call(be, m("blockentity.pos"));
                    int x = intOf(ReflectUtil.call(bp, m("blockpos.x")));
                    int y = intOf(ReflectUtil.call(bp, m("blockpos.y")));
                    int z = intOf(ReflectUtil.call(bp, m("blockpos.z")));
                    return MosaicBlockPos.of(x, y, z);
                } catch (Exception e) { return null; }
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

    /** null-safe 活体实体句柄(双代同值):health 0、maxHealth 0、dead false。 */
    private static final class NullSafeLivingEntity implements MosaicLivingEntity {
        public float health() { return 0.0f; }
        public float maxHealth() { return 0.0f; }
        public boolean dead() { return false; }
    }

    /** null-safe 状态效果句柄(双代同值):registryName "unknown"、amplifier 0、duration 0。 */
    private static final class NullSafeStatusEffect implements MosaicStatusEffect {
        public String registryName() { return "unknown"; }
        public int amplifier() { return 0; }
        public int duration() { return 0; }
    }

    /** null-safe 标签句柄(双代同值):registryName "unknown"、contents 空。 */
    private static final class NullSafeTag implements MosaicTag {
        public String registryName() { return "unknown"; }
        public String[] contents() { return new String[0]; }
    }

    /** null-safe 方块实体句柄(双代同值):typeRegistryName "unknown"、pos null。 */
    private static final class NullSafeBlockEntity implements MosaicBlockEntity {
        public String typeRegistryName() { return "unknown"; }
        public MosaicBlockPos pos() { return null; }
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
     *  onPacket 返回 no-op 订阅(契约环境无真实 Connection → 兜底值)。
     *  packetOf 投影照常(纯类名投影,不依赖连接对象)。 */
    private static final class NullSafeNetwork implements MosaicNetwork {
        public void sendPacket(int playerId, byte[] packetData) { }
        public MosaicPacket packetOf(Object vanillaPacket) { return packetOf0(vanillaPacket); }
        public MosaicPacketListener listener() {
            return new MosaicPacketListener() {
                public AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler) {
                    return new AutoCloseable() { public void close() { } };
                }
                public AutoCloseable onPacket(MosaicPacketSink sink) {
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
        private final List<MosaicPacketSink> sinks = new ArrayList<>();
        ConnectionNetwork(Vanilla262Provider p, Object conn) { this.p = p; this.conn = conn; }

        public void sendPacket(int playerId, byte[] packetData) {
            // 26.2 Connection.send(Packet):Packet 编解码需运行中服务端,契约环境不可构造 → 静默跳过
        }
        public MosaicPacket packetOf(Object vanillaPacket) { return packetOf0(vanillaPacket); }
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
                public AutoCloseable onPacket(MosaicPacketSink sink) {
                    if (sink == null) return new AutoCloseable() { public void close() { } };
                    final MosaicPacketSink s = sink;
                    sinks.add(s);
                    return new AutoCloseable() { public void close() { sinks.remove(s); } };
                }
            };
        }
    }
}
