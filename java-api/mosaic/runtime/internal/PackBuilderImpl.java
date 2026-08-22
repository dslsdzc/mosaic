package mosaic.runtime.internal;

/**
 * M5-2 内部实现占位(Task 2 纯接口阶段仅为满足编译):
 * MosaicPackBuilder.create 的转发目标,由 M5-2 实现(JNI 逐记录调用 C builder)。
 * 本占位仅抛错,运行时不可用。
 */
public final class PackBuilderImpl {
    private PackBuilderImpl() {}

    public static mosaic.runtime.MosaicPackBuilder create(String path, long moduleCount, long fnCount,
                                                          long triggerCount, long depCount, int eventCount) {
        throw new UnsupportedOperationException("PackBuilderImpl.create 由 M5-2 实现");
    }
}
