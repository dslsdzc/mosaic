package mosaic.runtime;

public interface MosaicServiceRef {
    MosaicService service();
    void release();
}
