package mosaic;

/** Mosaic API 基座:版本常量 + 版本守卫。API 只增不减:本常量只升不降。 */
public final class MosaicApi {
    /** 当前 API 版本(只增不减;新增成员用 @Since 标注) */
    public static final int API_VERSION = 1;
    private MosaicApi() {}

    /** 运行时守卫:mod 声明所需版本 > API_VERSION 时抛 MosaicApiVersionException。 */
    public static void requireApi(int requiredMax) {
        if (requiredMax > API_VERSION)
            throw new MosaicApiVersionException("API " + requiredMax + " required, runtime has " + API_VERSION);
    }

    /** 打开运行时(工厂;pack 路径数组,至少一个)。 */
    public static mosaic.runtime.MosaicRuntime open(String[] packPaths) {
        return mosaic.runtime.MosaicRuntime.open(packPaths);
    }
}
