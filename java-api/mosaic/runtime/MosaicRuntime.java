package mosaic.runtime;

import mosaic.MosaicApiException;
import mosaic.Since;

/** 运行时入口:打开/关闭/挂载 pack、派发、查询。实现经 JNI 到 C 内核。 */
public interface MosaicRuntime {
    /** 打开 pack 组(至少一个;事件表必须一致、模块范围不重叠)。 */
    static MosaicRuntime open(String[] packPaths) {
        return mosaic.runtime.internal.RuntimeImpl.open(packPaths);
    }
    long functionCount();
    /** 事件名 → id;未注册返回 -1。 */
    int eventId(String name);
    /** 派发事件(载荷 byte[] 小端 ↔ events.h 结构体);返回执行数。 */
    int eventDispatch(int eventId, byte[] payload);
    int workingSetCount();
    int lastError();
    /** 世界内挂载 pack(零重启);失败抛 MosaicHandleException。 */
    void addPack(String packPath);
    void close();

    MosaicFunctionLifecycle lifecycle();
    MosaicEventDispatcher eventDispatcher();
    MosaicEventCatalog eventCatalog();
    MosaicIndexQuery index();
    MosaicWorkingSet workingSet();
    MosaicModuleLoader moduleLoader();
    MosaicDependencyResolver dependencyResolver();
    MosaicScheduler scheduler();
    MosaicResourceManager resources();
    MosaicServiceRegistry services();
    MosaicQueryBuilder query();
}
