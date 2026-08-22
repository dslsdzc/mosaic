package mosaic.runtime.internal;

import mosaic.runtime.MosaicCheckpoint;
import mosaic.runtime.MosaicTask;

/** 检查点:NOOP 常量——save(task) 由调用方实现保存语义,默认不保存
 *  (取消/暂停时无状态落盘需求)。 */
public final class CheckpointImpl implements MosaicCheckpoint {
    public static final MosaicCheckpoint NOOP = new CheckpointImpl();

    private CheckpointImpl() {}

    public void save(MosaicTask task) { /* no-op:调用方未实现保存语义 */ }
}
