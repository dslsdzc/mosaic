package mosaic;

/**
 * M4-1:JVM ↔ C 运行时双向通道(JNI bridge)。
 *
 * 稳定 API 面:本类声明即 Java 侧稳定契约(设计规格第 24 节 Minecraft JVM
 * ↕ Minimal Bridge ↕ C Runtime;Stable Runtime ABI)。所有方法直映射
 * mosaic_runtime_* / mosaic_event_dispatch,零 Minecraft 依赖,纯本地可测。
 *
 * 载荷约定:eventDispatch 的 payload byte[] 与 C 侧事件载荷结构体
 * (include/mosaic/events.h)字节序一致(小端,LE),payload 长度 == 对应
 * 结构体大小。例:方块事件(block_break/block_place/block_interact/
 * block_tick)= 20B(player_id/x/y/z/block_type):u32 player_id, u32 x,
 * u32 y, u32 z, u32 block_type
 * (5 × 4B,LE);玩家事件(player_join/player_leave/player_death/...)=
 * 4B:u32 player_id。
 */
public final class Bridge {
    static {
        System.loadLibrary("mosaic_jni");
    }

    private Bridge() {}

    /* 打开 pack 组(可多个 pack 合并,同 C 侧 open_many),返回运行时句柄
       (0 = 失败;错误码经 lastError 查询)。 */
    public static native long runtimeOpen(String[] packPaths);

    /* 关闭运行时;句柄失效后所有调用安全返回 0/-1。 */
    public static native void runtimeClose(long rt);

    /* 函数总数(全部 pack 合并计数)。 */
    public static native long functionCount(long rt);

    /* 事件名 → id(包内排序位置);未注册返回 -1。 */
    public static native int eventId(long rt, String name);

    /* 派发事件;payload = 事件载荷字节(小端,见类注释);返回执行数
       (该事件订阅者中被执行的函数数)。 */
    public static native int eventDispatch(long rt, int eventId, byte[] payload);

    /* 工作集大小(已物化 ACTIVE 函数数)。 */
    public static native int workingSetCount(long rt);

    /* 最后错误码(0 = 无错;语义见 include/mosaic/base.h MOSAIC_ERR_*)。 */
    public static native int lastError(long rt);
}
