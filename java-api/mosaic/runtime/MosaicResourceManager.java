package mosaic.runtime;

import mosaic.Since;

public interface MosaicResourceManager {
    MosaicResourceLease acquire(long fnId);
    void release(MosaicResourceLease lease);
    /** 租约(MosaicLease 独立接口形态:fnId + release;与 acquire 同 native,
     *  release 语义同 close,幂等)。 */
    @Since(1)
    MosaicLease lease(long fnId);
}
