package mosaic.runtime;

public interface MosaicModuleLoader {
    MosaicModule load(long moduleId);
    void unload(long moduleId);
}
