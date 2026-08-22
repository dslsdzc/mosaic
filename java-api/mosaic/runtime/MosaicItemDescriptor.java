package mosaic.runtime;

public interface MosaicItemDescriptor {
    long providerFnId();
    String name();
    String tags();
    int category();
    String iconRef();
}
