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

    /* M4-3:世界内动态加载——向已打开实例追加一个 pack(零重启);0 成功,
       -1 失败(错误码经 lastError 查询;校验与 open_many 单 pack 一致:
       格式、事件表与 pack 0 一致、模块范围不重叠)。挂载后既有派发立即
       覆盖新 pack 的订阅者。 */
    public static native int runtimeAddPack(long rt, String packPath);

    /* 当前已挂载 pack 数。 */
    public static native int packCount(long rt);

    /* 最后错误码(0 = 无错;语义见 include/mosaic/base.h MOSAIC_ERR_*)。 */
    public static native int lastError(long rt);

    /* ---- M5:函数生命周期 ---- */
    public static native long fnMaterialize(long rt, long fnId);
    public static native int fnTombstone(long rt, long fnHandle);
    public static native void fnExecute(long rt, long fnHandle, int eventId, byte[] payload);
    public static native byte[] fnState(long rt, long fnHandle);
    public static native long fnIdOf(long rt, long fnHandle);
    /* ---- M5:pack 构建器 ---- */
    public static native long packCreate(String path, long moduleCount, long fnCount,
                                         long triggerCount, long depCount, int eventCount);
    public static native void packAddEvent(long b, String name);
    public static native void packAddModule(long b, long moduleId, int version, String name, String soPath);
    public static native void packAddFn(long b, long moduleId, long localId, int codeOff, int stateSize,
                                        int generation, int costHint, int flags);
    public static native void packSetFnTransform(long b, long fnId, int transformIndex);
    public static native void packAddTrigger(long b, int eventId, long fnId);
    public static native void packAddDep(long b, long ownerId, long depId);
    public static native void packSetItemCount(long b, long itemCount);
    public static native void packAddItem(long b, long providerFnId, String name, String tags,
                                          int category, String iconRef, int flags);
    public static native int packFinish(long b);
    public static native void packFree(long b);
    /* ---- M5:查询(描述符读字段,直通访问器) ---- */
    public static native long fnDescriptor(long rt, long fnId);       /* 0 = 未命中 */
    public static native long fnDescField(long rt, long desc, int field);  /* 字段:0=id 1=module 2=codeOff 3=gen 4=stateSize 5=cost 6=flags */
    public static native long moduleDescriptor(long rt, long moduleId);
    public static native long modDescField(long rt, long desc, int field); /* 0=id 1=version 2=gen */
    public static native String modDescString(long rt, long desc, int field); /* 0=name 1=soPath */
    /* ---- M5:item 描述符查询(直通 descriptor.c) ---- */
    public static native long itemCount(long rt);
    public static native long itemDescriptor(long rt, int category, String name);
    public static native long itemDescField(long rt, long desc, int field); /* 0=provider 1=category */
    public static native String itemDescString(long rt, long desc, int field); /* 0=name 1=tags 2=icon */
    public static native long[] itemForEachCategory(long rt, int category);  /* 分类内全部记录指针(冷态) */
    /* ---- M5:模块装载/依赖/驱逐/租约 ---- */
    public static native long moduleLoad(long rt, long moduleId);   /* ABI 指针;0 = 失败 */
    public static native void moduleUnload(long rt, long moduleId);
    public static native long moduleCount(long rt);
    public static native int depResolve(long rt, long moduleId, int minVer, int maxVer, long[] out);
    public static native int evictIdle(long rt, long windowNanos);
    public static native long[] activeFnIds(long rt);
    public static native long leaseAcquire(long rt, long fnId);
    public static native void leaseRelease(long lease);
    /* ---- M5:事务(补丁 pack 滚动更新) ---- */
    public static native long txBegin(long rt, String patchPath);
    public static native int txValidate(long tx);
    public static native int txCommit(long tx);
    public static native int txRollback(long tx);
    public static native void txAbort(long tx);
    public static native void txFree(long tx);
}
