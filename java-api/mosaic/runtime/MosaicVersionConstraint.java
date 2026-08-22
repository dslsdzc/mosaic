package mosaic.runtime;

public interface MosaicVersionConstraint {
    int minVersion();
    int maxVersion();   /* 0 = 无界 */
    boolean accepts(int version);
}
