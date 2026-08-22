package mosaic.runtime.internal;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.MosaicCapability;
import mosaic.runtime.MosaicCapabilityProvider;
import mosaic.runtime.MosaicCapabilityQuery;

/** 能力注册表(纯 Java 层):Class → MosaicCapabilityProvider 映射。
 *  require = 必需,缺失抛 MosaicProviderNotFoundException;
 *  optional = 可选,缺失返回 null。provider 经 register 挂载(注册在实现上,
 *  查询接口 MosaicCapabilityQuery 保持 query-only)。 */
public final class CapabilityImpl implements MosaicCapabilityQuery {
    private final Map<Class<?>, MosaicCapabilityProvider> providers = new ConcurrentHashMap<>();

    /** 注册能力 provider(按能力类型)。 */
    public void register(Class<? extends MosaicCapability> type, MosaicCapabilityProvider provider) {
        if (type == null || provider == null) throw new IllegalArgumentException("type and provider required");
        providers.put(type, provider);
    }

    @SuppressWarnings("unchecked")
    public <T extends MosaicCapability> T require(Class<T> type) {
        T t = optional(type);
        if (t == null)
            throw new MosaicProviderNotFoundException("no capability provider for " + type.getName());
        return t;
    }

    @SuppressWarnings("unchecked")
    public <T extends MosaicCapability> T optional(Class<T> type) {
        MosaicCapabilityProvider p = providers.get(type);
        return p == null ? null : (T) p.provide(type);
    }
}
