import mosaic.vanilla.MosaicProvider;

/** 版本环境:构造原版对象 + 注册 Provider(26.2/1.8.9 各一个实现)。 */
public interface VanillaEnv {
    MosaicProvider provider();
    Object worldObject() throws Exception;
    Object blockObject() throws Exception;
    Object itemObject() throws Exception;
    Object nbtObject() throws Exception;
    Object registryObject() throws Exception;
}
