package mosaic.runtime.internal;

import java.util.concurrent.ConcurrentHashMap;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.MosaicService;
import mosaic.runtime.MosaicServiceRegistry;

/** 服务注册/发现(纯 Java 层;get = 必需,缺失抛 ProviderNotFound)。 */
public final class ServiceRegistryImpl implements MosaicServiceRegistry {
    private final ConcurrentHashMap<Class<?>, MosaicService> services = new ConcurrentHashMap<>();

    public <T extends MosaicService> void register(Class<T> type, T service) {
        services.put(type, service);
    }

    public <T extends MosaicService> T get(Class<T> type) {
        T s = optional(type);
        if (s == null)
            throw new MosaicProviderNotFoundException("no service registered for " + type.getName());
        return s;
    }

    @SuppressWarnings("unchecked")
    public <T extends MosaicService> T optional(Class<T> type) {
        return (T) services.get(type);
    }
}
