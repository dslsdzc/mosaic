package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicFunctionState;

/** 函数状态读写实现(M6-B):read = materialize → fnState;write = materialize →
 *  fnStateWrite。失败语义:materialize 失败(read 返回 null / write 抛句柄异常,
 *  与 LifecycleImpl 同一纪律);fnStateWrite 超长(> state_size)拒绝 → -1 →
 *  write 抛 MosaicHandleException。持 RuntimeImpl(native 句柄与 lastError 来源)。
 */
public final class FunctionStateImpl implements MosaicFunctionState {
    private final RuntimeImpl rt;

    FunctionStateImpl(RuntimeImpl rt) { this.rt = rt; }

    /** 读函数状态:物化后读取;未物化/物化失败(fnId 未知等)→ null。 */
    public byte[] read(long fnId) {
        long h = Native.fnMaterialize(rt.handle(), fnId);
        if (h == 0) return null;
        return Native.fnState(rt.handle(), h);
    }

    /** 写函数状态:物化后写入;长度超 state_size → -1 → 抛 MosaicHandleException。 */
    public void write(long fnId, byte[] state) {
        if (state == null)
            throw new MosaicHandleException("state is null");
        long h = Native.fnMaterialize(rt.handle(), fnId);
        if (h == 0)
            throw new MosaicHandleException("state write: materialize failed (lastError="
                                            + Native.lastError(rt.handle()) + ")");
        if (Native.fnStateWrite(rt.handle(), h, state) != 0)
            throw new MosaicHandleException("fnStateWrite failed (lastError="
                                            + Native.lastError(rt.handle()) + ")");
    }
}
