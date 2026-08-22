package mosaic.runtime;

public interface MosaicItemIndex {
    /** 分类内按名二分;未命中 null。 */
    MosaicItemDescriptor find(int category, String name);
    /** 枚举分类内全部(回调返回 false 停止)。 */
    void forEach(int category, java.util.function.Consumer<MosaicItemDescriptor> consumer);
    long count();
}
