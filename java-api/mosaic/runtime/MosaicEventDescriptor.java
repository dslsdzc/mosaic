package mosaic.runtime;

public interface MosaicEventDescriptor {
    String name();
    /** 0=低 1=中 2=高 */
    int freq();
    int payloadSize();
}
