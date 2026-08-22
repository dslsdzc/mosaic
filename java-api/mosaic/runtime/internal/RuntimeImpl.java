package mosaic.runtime.internal;

/**
 * M5-2 内部实现占位(Task 2 纯接口阶段仅为满足编译):
 * MosaicRuntime.open 的转发目标,由 M5-2 实现(经 JNI 到 C 内核)。本占位仅抛错,
 * 运行时不可用。
 */
public final class RuntimeImpl {
    private RuntimeImpl() {}

    public static mosaic.runtime.MosaicRuntime open(String[] packPaths) {
        throw new UnsupportedOperationException("RuntimeImpl.open 由 M5-2 实现");
    }
}
