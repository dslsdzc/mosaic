package com.mosaic.agent;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.instrument.Instrumentation;

/**
 * M4-2:1.20.1 服务端注入 agent 入口(premain)。
 *
 * 注入机制 = JVM 标准 Instrumentation API(javaagent),零加载器依赖:
 *   1) 用 Bridge 打开世界 pack 运行时(失败不阻断服务端启动);
 *   2) 注册自研 ClassFileTransformer(com.mosaic.agent.MosaicTransformer);
 *   3) 类加载时注入 hook 调用(PlayerList 加入/离开、破坏方块、命令、服务端 tick)。
 *
 * 原生库加载:agent jar 内嵌 libmosaic_jni.so(构建脚本并入)→ premain 解出到临时
 * 目录并把该目录设为 java.library.path(ClassLoader 的 sys_paths 惰性初始化,
 * premain 阶段尚未触发,故属性生效)→ mosaic.Bridge 静态块的
 * System.loadLibrary("mosaic_jni") 命中。零外部运行时路径依赖。
 */
public final class MosaicAgent {

    private MosaicAgent() {}

    public static void premain(String args, Instrumentation inst) {
        /* 1) 原生库:解出内嵌 .so → mosaic.jni.lib 绝对路径属性
              (agent 内嵌版 Bridge 的静态块 System.load 该路径;
               JDK 的 java.library.path 在 premain 前已缓存,setProperty 无效) */
        try {
            File libDir = prepareNativeLibDir();
            System.setProperty("mosaic.jni.lib",
                    new File(libDir, "libmosaic_jni.so").getAbsolutePath());
            System.out.println("Mosaic agent: native lib="
                    + System.getProperty("mosaic.jni.lib"));
        } catch (Throwable t) {
            System.out.println("Mosaic agent: WARN native lib setup failed: " + t);
        }

        /* 2) 可见性:1.20.1 bundler 自建 URLClassLoader(父 = platform 加载器)
              加载服务端,系统加载器/agent 类对它不可见。把 agent jar 追加到
              bootstrap 搜索路径后,任何加载器(含 bundler 链)经委托
              (bundler→platform→bootstrap)都能解析 MosaicHooks 与 mosaic.Bridge,
              且是单一实例——premain 初始化的静态状态即注入 hook 所见状态。
              必须在引用 MosaicHooks/Bridge 之前执行。 */
        try {
            File jar = new File(MosaicAgent.class.getProtectionDomain()
                    .getCodeSource().getLocation().toURI());
            inst.appendToBootstrapClassLoaderSearch(new java.util.jar.JarFile(jar));
            System.out.println("Mosaic agent: agent jar appended to bootstrap search ("
                    + jar.getName() + ")");
        } catch (Throwable t) {
            System.out.println("Mosaic agent: WARN appendToBootstrap failed: " + t);
        }

        /* 3) pack 路径:agent args 指定,缺省 mc-server/packs/world.pack */
        String packArg = "mc-server/packs/world.pack";
        if (args != null && !args.trim().isEmpty()) packArg = args.trim();
        String pack = resolvePack(packArg);
        System.out.println("Mosaic agent: pack=" + pack);

        /* 4) 打开运行时;失败打印错误但继续(不阻断服务端) */
        boolean opened = false;
        try {
            long rt = mosaic.Bridge.runtimeOpen(new String[]{pack});
            if (rt != 0) {
                long n = mosaic.Bridge.functionCount(rt);
                MosaicHooks.init(rt);
                opened = true;
                System.out.println("Mosaic agent: runtime opened (functions=" + n
                        + "), transformer registered");
                /* Task 3:事件监听器(Java 观测通道)启动注册——-Dmosaic.listen
                   系统属性,逗号分隔事件名;内置监听器打印每包派发
                   (event/executed/payload hex)。E2E:
                   -Dmosaic.listen=packet_received,packet_sent */
                String listen = System.getProperty("mosaic.listen", "").trim();
                if (!listen.isEmpty()) {
                    for (String name : listen.split(",")) {
                        name = name.trim();
                        if (name.isEmpty()) continue;
                        boolean ok = MosaicHooks.registerListener(name,
                                (ev, executed, payload) -> System.out.println(
                                        "Mosaic agent: LISTENER " + ev
                                                + " executed=" + executed
                                                + " payload=" + MosaicHooks.hex(payload)));
                        System.out.println(ok
                                ? "Mosaic agent: listener registered for " + name
                                : "Mosaic agent: WARN listener not registered (unknown or unregistered event) " + name);
                    }
                }
            } else {
                System.out.println("Mosaic agent: ERROR runtimeOpen failed, lastError="
                        + mosaic.Bridge.lastError(rt) + " (continuing without bridge)");
            }
        } catch (Throwable t) {
            System.out.println("Mosaic agent: ERROR runtimeOpen exception: "
                    + t + " (continuing without bridge)");
        }

        /* 5) 注册 transformer(无论运行时是否打开) */
        try {
            inst.addTransformer(new MosaicTransformer(), true);
            if (!opened) {
                System.out.println("Mosaic agent: transformer registered"
                        + " (runtime not opened — hooks will no-op)");
            }
        } catch (Throwable t) {
            System.out.println("Mosaic agent: ERROR addTransformer: " + t);
        }
    }

    /* 解出 agent jar 内嵌的 libmosaic_jni.so 到临时目录(文件名必须恰为
       libmosaic_jni.so——loadLibrary 按 mapLibraryName 拼接查找)。
       内嵌缺失时回退 agent jar 同目录。 */
    private static File prepareNativeLibDir() throws Exception {
        try (InputStream in = MosaicAgent.class.getResourceAsStream("/libmosaic_jni.so")) {
            if (in != null) {
                File dir = java.nio.file.Files.createTempDirectory("mosaic_native").toFile();
                File lib = new File(dir, "libmosaic_jni.so");
                try (OutputStream out = new FileOutputStream(lib)) {
                    byte[] buf = new byte[65536];
                    int r;
                    while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
                }
                lib.deleteOnExit();
                dir.deleteOnExit();
                return dir;
            }
        }
        File jarDir = new File(MosaicAgent.class.getProtectionDomain().getCodeSource()
                .getLocation().toURI()).getParentFile();
        File sibling = new File(jarDir, "libmosaic_jni.so");
        if (sibling.isFile()) return jarDir;
        throw new java.io.IOException("libmosaic_jni.so not found (embedded or next to agent jar)");
    }

    /* pack 路径解析:原样 → user.dir 相对 → user.dir 父目录相对。
       (server 启动 cwd = mc-server 时,缺省 mc-server/packs/world.pack
        经第三级落到项目根下的 mc-server/packs/world.pack) */
    private static String resolvePack(String p) {
        File f = new File(p);
        if (f.isFile()) return f.getAbsolutePath();
        File cwd = new File(System.getProperty("user.dir"));
        File f2 = new File(cwd, p);
        if (f2.isFile()) return f2.getAbsolutePath();
        File parent = cwd.getParentFile();
        if (parent != null) {
            File f3 = new File(parent, p);
            if (f3.isFile()) return f3.getAbsolutePath();
        }
        return p;
    }
}
