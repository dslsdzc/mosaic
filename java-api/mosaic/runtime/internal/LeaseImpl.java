package mosaic.runtime.internal;

import mosaic.runtime.MosaicLease;

/** 资源租约(MosaicLease 独立接口形态;refs 守护):fnId + release →
 *  native leaseRelease(幂等,二次 release 无操作)。 */
public final class LeaseImpl implements MosaicLease {
    private final long fnId;
    private long lease;

    LeaseImpl(long fnId, long lease) {
        this.fnId = fnId;
        this.lease = lease;
    }

    public long fnId() { return fnId; }

    public void release() {
        if (lease != 0) { Native.leaseRelease(lease); lease = 0; }
    }
}
