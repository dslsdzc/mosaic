package mosaic.runtime;

public interface MosaicModuleIndex {
    MosaicModuleDescriptor find(long moduleId);
    long count();
}
