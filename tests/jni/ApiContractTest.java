import mosaic.MosaicApi;
import mosaic.runtime.*;
import mosaic.runtime.internal.Native;

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

        // 工作集驱逐
        MosaicWorkingSet ws = rt.workingSet();
        check(ws.count() == 2, "ws count 2");
        int evicted = ws.evictIdle(0);
        check(evicted >= 0, "evictIdle ok");
        check(ws.count() <= 2, "ws shrinks after evict");

        rt.close();

        // 版本守卫
        try { MosaicApi.requireApi(2); check(false, "requireApi(2) should throw"); }
        catch (mosaic.MosaicApiVersionException e) { check(true, "requireApi throws"); }

        if (failures == 0) System.out.println("API CONTRACT TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
