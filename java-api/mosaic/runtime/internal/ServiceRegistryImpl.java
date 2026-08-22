package mosaic.runtime.internal;

import java.util.concurrent.ConcurrentHashMap;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.MosaicService;
import mosaic.runtime.MosaicServiceRef;
import mosaic.runtime.MosaicServiceRegistry;

/** 服务注册/发现(纯 Java 层;get = 必需,缺失抛 ProviderNotFound)。
 *  值域放宽为 Object(Task 5:契约测试注册 Runnable 等非 MosaicService 类型)。
 *  M6-C:ref(Class) 服务引用——service 必须实现 MosaicService(注册表值域
 *  放宽的例外:非 MosaicService 类型无法构成 MosaicServiceRef,抛
 *  IllegalArgumentException);release 幂等,不删除注册条目。 */
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

    public <T> MosaicServiceRef ref(Class<T> type) {
        Object s = get(type);   /* 必需:缺失抛 ProviderNotFound(与 get 同语义) */
        if (!(s instanceof MosaicService))
            throw new IllegalArgumentException("service " + type.getName() + " is not a MosaicService");
        return new ServiceRefImpl((MosaicService) s);
    }

    static final class ServiceRefImpl implements MosaicServiceRef {
        private final MosaicService service;

        ServiceRefImpl(MosaicService service) { this.service = service; }

        public MosaicService service() { return service; }

        /* M6-D:死字段 released 已删(只写不读)。幂等语义不变:注册表条目不因
           ref 释放而删除(条目生命周期 = register 覆盖),release 无内部状态。 */
        public void release() { }
    }
}
