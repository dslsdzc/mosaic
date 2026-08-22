package mosaic.runtime;

public interface MosaicModuleDescriptor {
    long moduleId();
    int version();
    int generation();
    String name();
    String soPath();
}
