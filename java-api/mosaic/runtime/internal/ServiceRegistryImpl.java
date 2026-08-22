package mosaic.runtime.internal;

import java.util.concurrent.ConcurrentHashMap;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.MosaicService;
import mosaic.runtime.MosaicServiceRegistry;

/** 服务注册/发现(纯 Java 层;get = 必需,缺失抛 ProviderNotFound)。
 *  值域放宽为 Object(Task 5:契约测试注册 Runnable 等非 MosaicService 类型)。 */
public final class ServiceRegistryImpl implements MosaicServiceRegistry {
    private final ConcurrentHashMap<Class<?>, Object> services = new ConcurrentHashMap<>();

    public <T> void register(Class<T> type, T service) {
        services.put(type, service);
    }

    public <T> T get(Class<T> type) {
        T s = optional(type);
        if (s == null)
            throw new MosaicProviderNotFoundException("no service registered for " + type.getName());
        return s;
    }

    @SuppressWarnings("unchecked")
    public <T> T optional(Class<T> type) {
        return (T) services.get(type);
    }
}
