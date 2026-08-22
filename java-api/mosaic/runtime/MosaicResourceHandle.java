package mosaic.runtime;

public interface MosaicResourceHandle {
    boolean valid();
    void invalidate();
}
