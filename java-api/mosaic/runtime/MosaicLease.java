package mosaic.runtime;

/** 所有权:租约(refs 守护)。 */
public interface MosaicLease {
    long fnId();
    void release();
}
