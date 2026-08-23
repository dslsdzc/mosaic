import mosaic.vanilla.MosaicProvider;

/** 版本环境:构造原版对象 + 注册 Provider(26.2/1.8.9 各一个实现)。
 *  Entity/Player/Network 不加对象方法:三者需真实 Level/Connection 才能构造,
 *  契约环境不可构造——契约测试直接以 null 传参断言 Provider 的 null 语义
 *  (见 VanillaContractTest 注释)。Command 两代均可构造(26.2 CommandDispatcher /
 *  1.8.9 CommandHandler),提供 commandObject() 走真实路径。 */
public interface VanillaEnv {
    MosaicProvider provider();
    Object worldObject() throws Exception;
    Object blockObject() throws Exception;
    Object itemObject() throws Exception;
    Object nbtObject() throws Exception;
    Object registryObject() throws Exception;
    /** 无 World 依赖的可构造容器(26.2:SimpleContainer;1.8.9:InventoryBasic)。 */
    Object inventoryObject() throws Exception;
    /** 无服务器依赖的可构造命令对象(26.2:CommandDispatcher 无参;1.8.9:CommandHandler 隐式无参)。 */
    Object commandObject() throws Exception;
    /** 附魔对象(if-available 守卫:26.2 轻参构造 Enchantment 记录;1.8.9 静态实例
     *  Enchantment.sharpness);不可构造则抛 → 契约跳过真实路径断言。 */
    Object enchantmentObject() throws Exception;
    /** 状态效果实例对象(if-available 守卫:26.2 轻参构造 MobEffectInstance(Holder, int,
     *  int) / 1.8.9 轻参构造 PotionEffect(int, int, int));不可构造则抛 → 契约跳过
     *  真实路径断言。 */
    Object statusEffectObject() throws Exception;
    /** 标签对象(if-available 守卫:26.2 轻参构造 TagKey(ResourceKey, Identifier);
     *  1.8.9 无标签系统(jar 无 net.minecraft.tags 包)→ 抛 → 契约跳过真实路径断言)。 */
    Object tagObject() throws Exception;
    /** 网络对象(7.2):可构造的连接/网络管理器(26.2:Connection(PacketFlow);
     *  1.8.9:NetworkManager(EnumPacketDirection));不可构造 → 返回 null
     *  (Provider 返回 null-safe 网络句柄,packetOf 投影与 listener 注册语义照常)。 */
    Object networkObject() throws Exception;
    /** 可构造的包对象(7.2 共同包语义角色 → 该代真实包实例;26.2 mojmap 类 /
     *  1.8.9 MCP 类);角色不可构造 → 抛 → 契约跳过该角色真实路径断言(NOTE,
     *  与 commandObject/enchantmentObject 的 if-available 守卫同款)。 */
    Object packetObject(String role) throws Exception;
    /** 目录外包对象(7.2:未知包 → typeId 0;26.2 独有 / 1.8.9 独有包类,
     *  双代均为 clientbound 类 → 未知包 direction 断言 OUT)。 */
    Object unknownPacketObject() throws Exception;
}
