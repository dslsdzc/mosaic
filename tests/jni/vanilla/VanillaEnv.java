import mosaic.vanilla.MosaicProvider;

/** 版本环境:构造原版对象 + 注册 Provider(26.2/1.8.9 各一个实现)。
 *  Entity/Player 不加对象方法:两者需真实 Level 才能构造,契约环境不可构造——
 *  契约测试直接以 null 传参断言 Provider 的 null 语义(见 VanillaContractTest 注释)。 */
public interface VanillaEnv {
    MosaicProvider provider();
    Object worldObject() throws Exception;
    Object blockObject() throws Exception;
    Object itemObject() throws Exception;
    Object nbtObject() throws Exception;
    Object registryObject() throws Exception;
    /** 无 World 依赖的可构造容器(26.2:SimpleContainer;1.8.9:InventoryBasic)。 */
    Object inventoryObject() throws Exception;
}
