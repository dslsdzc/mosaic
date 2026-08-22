package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.*;

public final class RuntimeImpl implements MosaicRuntime {
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
    private final QueryBuilderImpl queryBuilder = new QueryBuilderImpl(index);

    private RuntimeImpl(long rt) { this.rt = rt; }

    public static MosaicRuntime open(String[] paths) {
        if (paths == null || paths.length == 0)
            throw new MosaicHandleException("at least one pack path required");
        long h = Native.runtimeOpen(paths);
        if (h == 0) throw new MosaicHandleException("runtime open failed");
        return new RuntimeImpl(h);
    }

    long handle() { return rt; }

    public long functionCount() { return Native.functionCount(rt); }
    public int eventId(String name) { return Native.eventId(rt, name); }
    public int eventDispatch(int eventId, byte[] payload) { return Native.eventDispatch(rt, eventId, payload); }
    public int workingSetCount() { return Native.workingSetCount(rt); }
    public int lastError() { return Native.lastError(rt); }
    public void addPack(String packPath) {
        if (Native.runtimeAddPack(rt, packPath) != 0)
            throw new MosaicHandleException("addPack failed (lastError=" + Native.lastError(rt) + ")");
    }
    public void close() { Native.runtimeClose(rt); }

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
}
