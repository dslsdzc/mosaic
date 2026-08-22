package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicFunctionLifecycle;

/** 函数生命周期实现:materialize/execute/tombstone/state/fnIdOf 直通 Native。 */
public final class LifecycleImpl implements MosaicFunctionLifecycle {
    private final RuntimeImpl rt;

    LifecycleImpl(RuntimeImpl rt) { this.rt = rt; }

    public long materialize(long fnId) {
        long h = Native.fnMaterialize(rt.handle(), fnId);
        if (h == 0)
            throw new MosaicHandleException("materialize failed (lastError=" + Native.lastError(rt.handle()) + ")");
        return h;
    }

    public void execute(long fnHandle, int eventId, byte[] payload) {
        Native.fnExecute(rt.handle(), fnHandle, eventId, payload);
    }

    public int tombstone(long fnHandle) {
        return Native.fnTombstone(rt.handle(), fnHandle);
    }

    public byte[] state(long fnHandle) {
        return Native.fnState(rt.handle(), fnHandle);
    }

    public long fnIdOf(long fnHandle) {
        return Native.fnIdOf(rt.handle(), fnHandle);
    }
}
