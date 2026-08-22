package mosaic.runtime;

public interface MosaicCapabilityQuery {
    <T extends MosaicCapability> T require(Class<T> type);
    <T extends MosaicCapability> T optional(Class<T> type);
}
