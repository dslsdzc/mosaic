package mosaic.runtime;

public interface MosaicServiceRegistry {
    <T extends MosaicService> void register(Class<T> type, T service);
    <T extends MosaicService> T get(Class<T> type);
    /** 可选获取(不存在返回 null)。 */
    <T extends MosaicService> T optional(Class<T> type);
}
