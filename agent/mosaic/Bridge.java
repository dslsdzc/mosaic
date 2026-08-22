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
 * 源码复制自 java/mosaic/Bridge.java(M4-1);native 方法集
 * (runtimeOpen/runtimeClose/functionCount/eventId/eventDispatch/
 *  workingSetCount/lastError)与载荷约定(小端 byte[])不变。
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
}
