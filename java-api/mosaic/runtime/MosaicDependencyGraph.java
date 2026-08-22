package mosaic.runtime;

/** 依赖图:闭包/环检测/版本约束。 */
public interface MosaicDependencyGraph {
    /** 遍历直接依赖(cb 返回 false 停止)。 */
    void forEachDep(long moduleId, java.util.function.LongConsumer consumer);
}
