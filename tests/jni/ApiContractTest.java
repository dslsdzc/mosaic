import mosaic.MosaicApi;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.*;
import mosaic.runtime.internal.CapabilityImpl;
import mosaic.runtime.internal.Native;

/** Task 5 评审:Capability 域契约(注册 → require 命中 → optional miss → require miss 抛)。 */
class TestCapability implements MosaicCapability {
    String tag() { return "cap"; }
}

public class ApiContractTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) { System.err.println("usage: ApiContractTest <pack>"); System.exit(2); }
        String pack = args[0];

        MosaicRuntime rt = MosaicRuntime.open(new String[]{pack});
        check(rt.functionCount() == 3, "functionCount==3");
        check(rt.eventId("player_join") >= 0, "eventId player_join");
        check(rt.eventId("nope") == -1, "eventId nope");

        // 物化 → 执行 → state
        MosaicFunctionLifecycle lc = rt.lifecycle();
        long f0 = lc.materialize(0x100000000L);   // fn(1,0)
        check(f0 != 0, "materialize fn(1,0)");
        lc.execute(f0, rt.eventId("player_join"), new byte[4]);
        byte[] st = lc.state(f0);
        check(st != null && st.length == 64, "state 64B");
        check(java.nio.ByteBuffer.wrap(st).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(0) == 1,
              "state counter==1 after 1 exec");

        // 派发(2 订阅者)→ working set
        int n = rt.eventDispatch(rt.eventId("player_join"), new byte[4]);
        check(n == 2, "dispatch==2, got " + n);
        check(rt.workingSetCount() == 2, "workingSet==2, got " + rt.workingSetCount());

        // 墓碑 → 恢复
        long f1 = lc.materialize(0x100000001L);
        check(f1 != 0, "materialize fn(1,1)");
        check(lc.tombstone(f0) == 0, "tombstone f0");
        check(rt.workingSetCount() == 1, "workingSet==1 after tombstone");
        long f0b = lc.materialize(0x100000000L);
        check(f0b != 0, "restore f0");
        byte[] st2 = lc.state(f0b);
        /* 墓碑时刻 counter == 2(直执行 1 次 + 派发 1 次);恢复语义 = 完整保留
           墓碑时刻状态(与 C test_lifecycle 恢复断言一致) */
        check(java.nio.ByteBuffer.wrap(st2).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(0) == 2,
              "state preserved across tombstone (counter==2, tombstone-time value)");

        // 索引/描述符(冷态)
        MosaicIndexQuery idx = rt.index();
        MosaicFunctionDescriptor fd = idx.functions().find(0x100000000L);
        check(fd != null && fd.fnId() == 0x100000000L, "descriptor fnId");
        check(fd.generation() == 1, "descriptor generation");
        check(idx.modules().find(1L) != null, "module descriptor");
        MosaicModuleDescriptor md = idx.modules().find(1L);
        check(md.name() != null && md.name().length() > 0, "module name non-empty");

        // 事件目录
        MosaicEventCatalog cat = rt.eventCatalog();
        check(cat.find("player_join") != null, "catalog player_join");
        check(cat.find("zzz") == null, "catalog miss");

        // 工作集驱逐(Task 5 评审加固:原 ws.count() <= 2 是空断言)
        MosaicWorkingSet ws = rt.workingSet();
        check(ws.count() == 2, "ws count 2 before evict, got " + ws.count());
        int evicted = ws.evictIdle(0);
        /* 窗口 0 → 任何 last_use 都满足 (last_use + 0) <= now;f0b/f1 均
           refs==0(无租约)→ 全墓碑。实测:evicted==2、count==0。 */
        check(evicted == 2, "evictIdle(0) tombstones both idle fns, got " + evicted);
        check(ws.count() == 0, "ws empty after evict, got " + ws.count());

        // 模块加载器
        MosaicModuleLoader loader = rt.moduleLoader();
        MosaicModule mod = loader.load(1L);
        check(mod != null && mod.moduleId() == 1L, "module load");
        check(mod.name() != null && mod.name().length() > 0, "module name");
        loader.unload(1L);

        // 依赖解析(无依赖模块 → 闭包 = 自身)
        MosaicDependencyResolver dr = rt.dependencyResolver();
        long[] closure = dr.resolve(1L, null);
        check(closure.length >= 1, "dep resolve closure non-empty");

        // 事件 Java 订阅
        final int[] javaCalls = {0};
        MosaicEventDispatcher ed = rt.eventDispatcher();
        MosaicEventSubscription sub = ed.subscribe(rt.eventId("player_join"), (e, payload) -> javaCalls[0]++);
        ed.dispatch(rt.eventId("player_join"), new byte[4]);
        check(javaCalls[0] == 1, "java subscription called, got " + javaCalls[0]);
        sub.close();
        ed.dispatch(rt.eventId("player_join"), new byte[4]);
        check(javaCalls[0] == 1, "subscription closed stops calls");

        // 调度器(纯 Java)
        MosaicScheduler sched = rt.scheduler();
        final int[] done = {0};
        sched.submit(new MosaicTask() {
            public long id() { return 1; }
            public int[] dependencyIds() { return new int[0]; }
            public int priority() { return 0; }
            public int affinity() { return -1; }
            public void run() { done[0]++; }
            public MosaicCheckpoint checkpoint() { return null; }
        });
        sched.waitAll();
        check(done[0] == 1, "scheduler task ran");

        // 租约
        MosaicResourceManager rm = rt.resources();
        MosaicResourceLease lease = rm.acquire(0x100000000L);
        check(lease != null, "lease acquired");
        check(rt.workingSetCount() >= 1, "lease holds fn in ws");
        rm.release(lease);

        // 服务注册
        MosaicServiceRegistry sr = rt.services();
        sr.register(Runnable.class, () -> {});
        check(sr.get(Runnable.class) != null, "service get");
        check(sr.optional(String.class) == null, "service optional miss");

        // 能力注册表(纯 Java;注册在 internal 实现上,查询接口 query-only)
        CapabilityImpl caps = (CapabilityImpl) rt.capability();
        caps.register(TestCapability.class, new MosaicCapabilityProvider() {
            @SuppressWarnings("unchecked")
            public <T extends MosaicCapability> T provide(Class<T> type) { return (T) new TestCapability(); }
        });
        check(caps.require(TestCapability.class) != null, "capability require hit");
        check(caps.require(TestCapability.class).tag().equals("cap"), "capability provider invoked");
        check(caps.optional(MosaicCapability.class) == null, "capability optional miss");
        try { caps.require(MosaicCapability.class); check(false, "capability require miss should throw"); }
        catch (MosaicProviderNotFoundException e) { check(true, "capability require miss throws"); }

        // 查询(创造模式)
        MosaicQueryBuilder qb = rt.query();
        MosaicQuery q = qb.byCategory(0);
        check(q != null, "query by category");

        rt.close();

        // 版本守卫
        try { MosaicApi.requireApi(2); check(false, "requireApi(2) should throw"); }
        catch (mosaic.MosaicApiVersionException e) { check(true, "requireApi throws"); }

        if (failures == 0) System.out.println("API CONTRACT TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
