import java.util.Arrays;
import mosaic.MosaicApi;
import mosaic.MosaicHandleException;
import mosaic.MosaicProviderNotFoundException;
import mosaic.runtime.*;
import mosaic.runtime.internal.CapabilityImpl;
import mosaic.runtime.internal.EventPayloadImpl;
import mosaic.runtime.internal.Native;
import mosaic.runtime.internal.StateTransformImpl;
import mosaic.runtime.internal.TriggerEntryImpl;

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

        // ---- M6-A:事件载荷类型化解码 / 编解码工具 / 自诊断桥 ----
        // player 域 {player_id=7}(4B 小端)→ decodeInts()[0]==7(测试包仅注册
        // player_join(id 0),域判定走 EVENT_NAMES 探测路径)
        int join = rt.eventId("player_join");
        MosaicEventPayload pl = MosaicEventPayload.of(join, new byte[]{7, 0, 0, 0});
        check(pl instanceof EventPayloadImpl, "factory returns EventPayloadImpl");
        int[] fields = pl.decodeInts();
        check(fields.length == 1 && fields[0] == 7,
              "payload decode player_id==7, got " + Arrays.toString(fields));

        // encode→decode 回环
        byte[] enc = pl.encode();
        check(enc.length == 4 && Arrays.equals(enc, new byte[]{7, 0, 0, 0}),
              "payload encode round-trip");

        // 失败语义(规格 §8:解码失败 → MosaicHandleException)
        try { MosaicEventPayload.of(join, new byte[8]); check(false, "payload len mismatch should throw"); }
        catch (MosaicHandleException e) { check(true, "payload len mismatch throws"); }
        try { MosaicEventPayload.of(join, new byte[2]); check(false, "payload non-4-multiple should throw"); }
        catch (MosaicHandleException e) { check(true, "payload non-4-multiple throws"); }
        try { MosaicEventPayload.of(-1, new byte[4]); check(false, "unknown event id should throw"); }
        catch (MosaicHandleException e) { check(true, "unknown event id throws"); }

        // 编解码工具:encodeInts(1,2,3) → byte[12] → decodeInts 回环
        MosaicPayloadCodec codec = MosaicPayloadCodec.littleEndian();
        byte[] enc3 = codec.encodeInts(1, 2, 3);
        int[] back = codec.decodeInts(enc3);
        check(enc3.length == 12 && back.length == 3 && back[0] == 1 && back[2] == 3,
              "codec encodeInts(1,2,3) round-trip");
        int[] neg = codec.decodeInts(codec.encodeInts(-1, 0x7fffffff, 0x80000000));
        check(neg[0] == -1 && neg[1] == 0x7fffffff && neg[2] == 0x80000000,
              "codec preserves 32-bit range");

        // 自诊断桥:句柄 != 0、lastError 可读且与运行时同源
        MosaicBridge b = rt.bridge();
        check(b.nativeHandle() != 0, "bridge nativeHandle != 0");
        check(b.lastError() == rt.lastError(), "bridge lastError mirrors runtime");

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

        // ---- M6-B:状态域(函数状态读写 / 状态存储 / 状态迁移)----
        // fn(1,1)(code_add:counter += 载荷 u32)此前载荷全零 → counter 仍为 0;
        // execute({1,0,0,0}) ×1 后 state[0]==1(读回环起点)
        long rtH = rt.bridge().nativeHandle();
        long fw = lc.materialize(0x100000001L);
        check(fw != 0, "M6B materialize fn(1,1)");
        lc.execute(fw, join, new byte[]{1, 0, 0, 0});
        byte[] stW = lc.state(fw);
        check(java.nio.ByteBuffer.wrap(stW).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(0) == 1,
              "M6B state[0]==1 after 1 exec");

        // fnStateWrite 直通:写入 {42,0,...} → 读回 state[0]==42
        byte[] ov = new byte[64];
        ov[0] = 42;
        check(Native.fnStateWrite(rtH, fw, ov) == 0, "M6B fnStateWrite==0");
        byte[] stW2 = lc.state(fw);
        check(stW2[0] == 42, "M6B state[0]==42 after write, got " + (stW2[0] & 0xff));

        // 超长写入(> state_size=64)拒绝 → -1
        check(Native.fnStateWrite(rtH, fw, new byte[65]) == -1,
              "M6B fnStateWrite over-length rejected (-1)");

        // stateStore().forFn(fnId) 读写(公共 API 路径)
        MosaicStateStore store = rt.stateStore();
        MosaicFunctionState fs = store.forFn(0x100000001L);
        byte[] stR = fs.read(0x100000001L);
        check(stR != null && stR.length == 64 && stR[0] == 42,
              "M6B stateStore read == 42");
        byte[] ov2 = new byte[64];
        ov2[0] = 7;
        fs.write(0x100000001L, ov2);
        byte[] stW3 = lc.state(fw);
        check(stW3[0] == 7, "M6B stateStore write → state[0]==7, got " + (stW3[0] & 0xff));
        check(fs.read(0x99999999L) == null, "M6B stateStore read unknown fn → null");
        try { fs.write(0x100000001L, new byte[128]); check(false, "M6B store write over-length should throw"); }
        catch (MosaicHandleException e) { check(true, "M6B store write over-length throws"); }

        // 状态迁移钩子(纯 Java 语义)
        MosaicStateTransform noop = StateTransformImpl.noop();
        byte[] v1 = {1, 2, 3, 4};
        byte[] v2 = new byte[8];
        noop.transform(v1, v2, 8);
        check(v2[0] == 1 && v2[3] == 4 && v2[4] == 0 && v2[7] == 0,
              "M6B noop transform copies prefix, zero tail");
        byte[] v4 = new byte[2];
        noop.transform(v1, v4, 2);
        check(v4[0] == 1 && v4[1] == 2, "M6B noop transform size=2 truncates");
        MosaicStateTransform bump = (src, dst, sz) -> { noop.transform(src, dst, sz); dst[0] = (byte) (dst[0] + 1); };
        byte[] v3 = new byte[8];
        bump.transform(v1, v3, 8);
        check(v3[0] == 2 && v3[1] == 2 && v3[2] == 3 && v3[3] == 4,
              "M6B custom transform bump[0] → 2, got " + v3[0]);

        // ---- M6-B:触发域(事件 → 订阅函数 id 索引)----
        MosaicTriggerIndex ti = rt.triggerIndex();
        long[] subs = ti.subscribers(join);   // gen_test_pack:player_join 2 触发器
        check(subs.length == 2, "M6B subscribers(player_join)==2, got " + subs.length);
        check(subs.length >= 2 && subs[0] == 0x100000000L && subs[1] == 0x100000001L,
              "M6B subscribers are fn(1,0)/fn(1,1) sorted, got " + Arrays.toString(subs));
        check(ti.subscribers(12345).length == 0, "M6B unregistered event → empty array");
        MosaicTriggerEntry te = new TriggerEntryImpl(join, 0x100000000L);
        check(te.eventId() == join && te.fnId() == 0x100000000L,
              "M6B TriggerEntry data class eventId/fnId");

        rt.close();

        // 版本守卫
        try { MosaicApi.requireApi(2); check(false, "requireApi(2) should throw"); }
        catch (mosaic.MosaicApiVersionException e) { check(true, "requireApi throws"); }

        if (failures == 0) System.out.println("API CONTRACT TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
