package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicResourceLease;
import mosaic.runtime.MosaicResourceManager;

/** 资源管理实现:租约直通 mosaic_lease_acquire/release(refs 守护)。 */
public final class ResourceManagerImpl implements MosaicResourceManager {
    private final RuntimeImpl rt;

    ResourceManagerImpl(RuntimeImpl rt) { this.rt = rt; }

    public MosaicResourceLease acquire(long fnId) {
        long h = Native.leaseAcquire(rt.handle(), fnId);
        if (h == 0)
            throw new MosaicHandleException("lease acquire failed (lastError=" + Native.lastError(rt.handle()) + ")");
        return new ResourceLeaseImpl(fnId, h);
    }

    public void release(MosaicResourceLease lease) {
        if (lease instanceof ResourceLeaseImpl) ((ResourceLeaseImpl) lease).close();
    }

    static final class ResourceLeaseImpl implements MosaicResourceLease {
        private final long fnId;
        private long lease;
        ResourceLeaseImpl(long fnId, long lease) {
            this.fnId = fnId;
            this.lease = lease;
        }
        public long fnId() { return fnId; }
        public void close() {
            if (lease != 0) { Native.leaseRelease(lease); lease = 0; }
        }
    }
}
