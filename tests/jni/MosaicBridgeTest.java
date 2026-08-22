package mosaic.jni;

import mosaic.Bridge;

/**
 * M4-1:JVM Bridge 骨架测试(main 方法断言 + System.exit)。
 *
 * 前置:脚本 ci/run_jni_test.sh 先 CMake 构建(产出 build/lib/libmosaic_jni.so
 * 与 build/bench/synth_mod.so),再运行 build/gen_test_pack 生成测试 pack
 * (1 模块 3 函数、player_join 事件、2 个触发器),随后 javac + java 本类。
 *
 * 断言序列:
 *   1. runtimeOpen(生成 pack) → 句柄 != 0
 *   2. functionCount == 3;eventId("player_join") >= 0;eventId("nope") == -1
 *   3. eventDispatch(player_join, payload{player_id=7}) == 2(两个订阅者)
 *   4. workingSetCount == 2(派发后两个订阅函数均已物化)
 *   5. runtimeClose
 *   6. runtimeOpen(不存在路径) == 0;零句柄调用全部安全返回 0/-1
 */
public final class MosaicBridgeTest {

    private static int failures = 0;

    private static void check(boolean cond, String what) {
        System.out.println((cond ? "PASS " : "FAIL ") + what);
        if (!cond) failures++;
    }

    public static void main(String[] args) {
        String pack = args.length > 0 ? args[0] : "build/jni_test.pack";
        String pack2 = args.length > 1 ? args[1] : "build/jni_add.pack";
        System.out.println("MosaicBridgeTest: pack=" + pack + " pack2=" + pack2);

        /* 1. 打开生成的 pack */
        long rt = Bridge.runtimeOpen(new String[]{pack});
        check(rt != 0, "runtimeOpen(" + pack + ") != 0");
        if (rt == 0) {
            System.out.println("rt==0; lastError=" + Bridge.lastError(rt));
            System.exit(1);
        }

        /* 2. 函数总数 / 事件 id 解析 */
        check(Bridge.functionCount(rt) == 3,
              "functionCount == 3 (got " + Bridge.functionCount(rt) + ")");
        int join = Bridge.eventId(rt, "player_join");
        check(join >= 0, "eventId(player_join) >= 0 (got " + join + ")");
        check(Bridge.eventId(rt, "nope") == -1,
              "eventId(nope) == -1 (got " + Bridge.eventId(rt, "nope") + ")");

        /* 3. 派发 player_join(载荷 player_id=7,4B 小端)→ 2 个订阅者执行 */
        byte[] payload = {7, 0, 0, 0};
        int n = Bridge.eventDispatch(rt, join, payload);
        check(n == 2, "eventDispatch(player_join) == 2 (got " + n + ")");

        /* 4. 工作集:两个订阅函数派发中物化(ACTIVE) */
        check(Bridge.workingSetCount(rt) == 2,
              "workingSetCount == 2 (got " + Bridge.workingSetCount(rt) + ")");

        /* 5. 关闭 */
        Bridge.runtimeClose(rt);

        /* 6. 失败路径 / 零句柄安全 */
        long bad = Bridge.runtimeOpen(new String[]{pack + ".does-not-exist"});
        check(bad == 0, "runtimeOpen(missing) == 0 (got " + bad + ")");
        check(Bridge.eventDispatch(0, join, payload) == 0, "eventDispatch(0, ...) == 0");
        check(Bridge.eventId(0, "player_join") == -1, "eventId(0, ...) == -1");
        check(Bridge.functionCount(0) == 0, "functionCount(0) == 0");
        check(Bridge.workingSetCount(0) == 0, "workingSetCount(0) == 0");
        check(Bridge.lastError(0) == 0, "lastError(0) == 0");
        Bridge.runtimeClose(0);   /* 零句柄关闭:no-op 不崩 */

        /* 7. M4-3:世界内动态加载 runtimeAddPack */
        long rt2 = Bridge.runtimeOpen(new String[]{pack});
        check(rt2 != 0, "runtimeOpen(main pack) != 0 (addPack case)");
        if (rt2 == 0) {
            System.out.println("rt2==0; lastError=" + Bridge.lastError(rt2));
            System.exit(1);
        }
        check(Bridge.functionCount(rt2) == 3,
              "functionCount == 3 before add (got " + Bridge.functionCount(rt2) + ")");
        check(Bridge.packCount(rt2) == 1,
              "packCount == 1 before add (got " + Bridge.packCount(rt2) + ")");
        check(Bridge.runtimeAddPack(rt2, pack2) == 0,
              "runtimeAddPack(pack2) == 0");
        check(Bridge.functionCount(rt2) == 5,
              "functionCount == 5 after add 3+2 (got " + Bridge.functionCount(rt2) + ")");
        check(Bridge.packCount(rt2) == 2,
              "packCount == 2 after add (got " + Bridge.packCount(rt2) + ")");
        /* 新 pack 订阅者立即参与派发:player_join 订阅 2 → 3 */
        int n2 = Bridge.eventDispatch(rt2, join, payload);
        check(n2 == 3, "eventDispatch == 3 after add (got " + n2 + ")");
        /* 失败:重复挂载主 pack(模块 1 与已挂载模块 1 范围重叠)→ -1 + lastError 非 0 */
        check(Bridge.runtimeAddPack(rt2, pack) == -1,
              "runtimeAddPack(overlap) == -1");
        check(Bridge.lastError(rt2) != 0,
              "lastError != 0 after failed add (got " + Bridge.lastError(rt2) + ")");
        check(Bridge.functionCount(rt2) == 5,
              "functionCount unchanged after failed add (got " + Bridge.functionCount(rt2) + ")");
        check(Bridge.packCount(rt2) == 2,
              "packCount unchanged after failed add (got " + Bridge.packCount(rt2) + ")");
        Bridge.runtimeClose(rt2);

        System.out.println(failures == 0 ? "ALL JNI TESTS PASSED"
                                         : failures + " JNI TEST(S) FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
