package mosaic.runtime;

public interface MosaicCapabilityProvider {
    <T extends MosaicCapability> T provide(Class<T> type);
}
