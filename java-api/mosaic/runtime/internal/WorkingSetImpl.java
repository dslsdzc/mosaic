package mosaic.runtime.internal;

import mosaic.runtime.MosaicWorkingSet;

/** 工作集实现:count/evictIdle/activeFnIds 直通 Native(驱逐 = mosaic_evict_idle)。 */
public final class WorkingSetImpl implements MosaicWorkingSet {
    private final RuntimeImpl rt;

    WorkingSetImpl(RuntimeImpl rt) { this.rt = rt; }

    public int count() { return Native.workingSetCount(rt.handle()); }

    public int evictIdle(long windowNanos) {
        return Native.evictIdle(rt.handle(), windowNanos);
    }

    public long[] activeFnIds() {
        long[] ids = Native.activeFnIds(rt.handle());
        return ids == null ? new long[0] : ids;
    }
}
