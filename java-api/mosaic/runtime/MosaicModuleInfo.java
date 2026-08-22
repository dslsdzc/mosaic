package mosaic.runtime;

public interface MosaicModuleInfo {
    long moduleId();
    int version();
    String soPath();
    int fnCount();
}
