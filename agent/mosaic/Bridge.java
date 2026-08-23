package mosaic;

/**
 * M4-2:注入 agent 内嵌版 Bridge——API 面与 M4-1 稳定契约
 * (java/mosaic/Bridge.java)完全一致,仅静态初始化加载策略不同:
 *
 *   JDK 的 java.library.path 在 premain 之前已被缓存(实测 JDK 21:
 *   ClassLoader.sys_paths 首用即固化,premain 里 setProperty 不生效),
 *   故 agent 版静态块优先按 premain 设置的绝对路径 System.load
 *   ("mosaic.jni.lib" 属性,MosaicAgent 解出内嵌 libmosaic_jni.so 后写入),
 *   缺失时回退 System.loadLibrary("mosaic_jni")。
 *
 * 源码复制自 java/mosaic/Bridge.java(M4-1);native 方法集与载荷约定
 * (小端 byte[])不变。全量方法集类别(M4-1 基座 7 个 + M4-3 热挂载 2 个
 * + M5 生命周期/构建器/查询/item 查询/模块装载/驱逐/租约/事务 + M6-C
 * 元数据 + M6-D 事件目录 + M9 派发超时 setDispatchTimeout),与
 * java/mosaic/Bridge.java 经 build_mc_agent.sh grep-diff 逐 native 声明
 * 一致性检查强制同步(不一致 → 构建失败)。
 */
public final class Bridge {
    static {
        String lib = System.getProperty("mosaic.jni.lib");
        if (lib != null && new java.io.File(lib).isFile()) {
            System.load(lib);
        } else {
            System.loadLibrary("mosaic_jni");
        }
    }

    private Bridge() {}

    public static native long runtimeOpen(String[] packPaths);

    public static native void runtimeClose(long rt);

    public static native long functionCount(long rt);

    public static native int eventId(long rt, String name);

    public static native int eventDispatch(long rt, int eventId, byte[] payload);

    public static native int workingSetCount(long rt);

    public static native int runtimeAddPack(long rt, String packPath);

    public static native int packCount(long rt);

    public static native int lastError(long rt);

    /* M9:每事件派发超时预算(微秒;0 = 不限制,默认 0)。预算只保护"慢函数
       不阻塞同事件其他订阅者"——超时点在订阅者边界,不能中断正在执行的
       函数;预算生效时超时后 lastError == MOSAIC_ERR_TIMEOUT。 */
    public static native void setDispatchTimeout(long rt, long us);

    /* ---- M5:函数生命周期 ---- */
    public static native long fnMaterialize(long rt, long fnId);
    public static native int fnTombstone(long rt, long fnHandle);
    public static native void fnExecute(long rt, long fnHandle, int eventId, byte[] payload);
    public static native byte[] fnState(long rt, long fnHandle);
    public static native long fnIdOf(long rt, long fnHandle);
    /* 写函数状态(物化后;state 内存 memcpy;长度超 state_size 拒绝返回 -1) */
    public static native int fnStateWrite(long rt, long fnHandle, byte[] state);
    /* 列出事件订阅者 fn_id(触发表区间扫描;返回长度,out 填充) */
    public static native int triggerSubscribers(long rt, int eventId, long[] out);
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
    public static native long modDescField(long rt, long desc, int field); /* 0=id 1=version 2=gen 3=fnCount */
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
    /* ---- M6-C:元数据(直接依赖遍历 / pack 计数 / tx 补丁信息) ---- */
    /* 直接依赖遍历:moduleId 的依赖模块 id 列表(单层,非闭包);out == null 探测
       返回总数,out != null 填充 min(cap, 总数) 条并返回实际写入数 */
    public static native int depForEach(long rt, long moduleId, long[] out);
    /* pack 信息计数:field 0=module 1=fn 2=trigger 3=item 4=event(全部基础
       pack 合并求和);非法 field 返回 -1 */
    public static native long packInfoCount(long rt, int field);
    /* tx 补丁函数 id 列表(补丁 pack fn 表,按 fn_id 排序);out == null 探测
       返回总数,out != null 填充 min(cap, 总数) 条并返回实际写入数 */
    public static native int txPatchFnIds(long tx, long[] out);
    /* ---- M6-D:事件目录访问器(契约门禁用) ---- */
    /* 目录第 index 个事件名(events.c 静态目录,与 EventImpl.EVENT_NAMES 同序);
       越界/负 index → null。契约测试遍历全部名字与 EVENT_NAMES 逐项比对。 */
    public static native String eventCatalogName(int index);
    /* M6-E:目录第 index 个包名(packets.c 静态目录,与
       PacketCatalogImpl.PACKET_NAMES 同序);越界/负 index → null。契约测试
       遍历全部名字与 PACKET_NAMES 逐项比对。 */
    public static native String packetCatalogName(int index);
}
