package mosaic.runtime;

public interface MosaicResourceLease {
    long fnId();
    void close();
}
