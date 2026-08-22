package mosaic.runtime;

public interface MosaicResourceManager {
    MosaicResourceLease acquire(long fnId);
    void release(MosaicResourceLease lease);
}
