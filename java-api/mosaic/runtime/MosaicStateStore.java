package mosaic.runtime;

public interface MosaicStateStore {
    MosaicFunctionState forFn(long fnId);
}
