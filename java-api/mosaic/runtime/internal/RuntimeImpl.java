package mosaic.runtime.internal;

import java.util.concurrent.ConcurrentHashMap;
import mosaic.MosaicHandleException;
import mosaic.runtime.*;

public final class RuntimeImpl implements MosaicRuntime {
    /* M6-A:活跃运行时登记(eventId → 名称反查用;事件 id 是包内排序位置,
       子集 pack 必须经运行时探测 Native.eventId;close 时注销) */
    private static final ConcurrentHashMap<Long, RuntimeImpl> LIVE = new ConcurrentHashMap<>();

    private final long rt;
    private final LifecycleImpl lifecycle = new LifecycleImpl(this);
    private final EventImpl events = new EventImpl(this);
    private final IndexImpl index = new IndexImpl(this);
    private final WorkingSetImpl ws = new WorkingSetImpl(this);
    private final ActivationGateImpl gate = new ActivationGateImpl();
    /* 有状态域必须每运行时单例(注册表/调度器/租约/模块装载跨调用保持状态) */
    private final ModuleLoaderImpl moduleLoader = new ModuleLoaderImpl(this);
    private final DependencyResolverImpl depResolver = new DependencyResolverImpl(this);
    private final SchedulerImpl scheduler = new SchedulerImpl();
    private final ResourceManagerImpl resources = new ResourceManagerImpl(this);
    private final ServiceRegistryImpl services = new ServiceRegistryImpl();
    private final CapabilityImpl capabilities = new CapabilityImpl();
    private final QueryBuilderImpl queryBuilder = new QueryBuilderImpl(index);
    private final BridgeImpl bridge = new BridgeImpl(this);
    /* M6-B:状态域 / 触发域(每运行时单例) */
    private final StateStoreImpl stateStore = new StateStoreImpl(this);
    private final TriggerIndexImpl triggerIndex = new TriggerIndexImpl(this);

    private RuntimeImpl(long rt) {
        this.rt = rt;
        LIVE.put(rt, this);
    }

    public static MosaicRuntime open(String[] paths) {
        if (paths == null || paths.length == 0)
            throw new MosaicHandleException("at least one pack path required");
        long h = Native.runtimeOpen(paths);
        if (h == 0) throw new MosaicHandleException("runtime open failed");
        return new RuntimeImpl(h);
    }

    long handle() { return rt; }

    /** 活跃运行时快照(EventPayloadImpl 反查 eventId → 名称用)。 */
    static RuntimeImpl[] live() { return LIVE.values().toArray(new RuntimeImpl[0]); }

    public long functionCount() { return Native.functionCount(rt); }
    public int eventId(String name) { return Native.eventId(rt, name); }
    public int eventDispatch(int eventId, byte[] payload) { return Native.eventDispatch(rt, eventId, payload); }
    public int workingSetCount() { return Native.workingSetCount(rt); }
    public int lastError() { return Native.lastError(rt); }
    public void addPack(String packPath) {
        if (Native.runtimeAddPack(rt, packPath) != 0)
            throw new MosaicHandleException("addPack failed (lastError=" + Native.lastError(rt) + ")");
    }
    public void close() {
        LIVE.remove(rt);
        Native.runtimeClose(rt);
    }

    public MosaicFunctionLifecycle lifecycle() { return lifecycle; }
    public MosaicEventDispatcher eventDispatcher() { return events; }
    public MosaicEventCatalog eventCatalog() { return events.catalog(); }
    public MosaicIndexQuery index() { return index; }
    public MosaicWorkingSet workingSet() { return ws; }
    public MosaicModuleLoader moduleLoader() { return moduleLoader; }
    public MosaicDependencyResolver dependencyResolver() { return depResolver; }
    public MosaicScheduler scheduler() { return scheduler; }
    public MosaicResourceManager resources() { return resources; }
    public MosaicServiceRegistry services() { return services; }
    public MosaicQueryBuilder query() { return queryBuilder; }
    public MosaicTransaction txBegin(String patchPath) { return TxImpl.begin(rt, patchPath); }
    public MosaicActivationGate activation() { return gate; }
    public MosaicCapabilityQuery capability() { return capabilities; }
    public MosaicBridge bridge() { return bridge; }
    public MosaicStateStore stateStore() { return stateStore; }
    public MosaicTriggerIndex triggerIndex() { return triggerIndex; }
}
