package com.mosaic.agent;

import mosaic.Bridge;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * M4-2:注入目标的静态 hook 集合(由 MosaicTransformer 在 vanilla 1.20.1 服务端
 * 方法内插入静态调用;所有方法 try-catch,注入代码不得崩服务端)。
 *
 * 类签名核实(2026-08-22,官方 server.jar 1.20.1 的 ProGuard 混淆名,经 Mojang
 * server_mappings 映射 + javap 逐项确认;Mojang 自 1.20.1 起对服务端 jar 做
 * ProGuard 混淆,mojmap 名在运行时不存在):
 *   - net.minecraft.server.players.PlayerList        -> alk
 *       placeNewPlayer(Connection,ServerPlayer)      -> a      (desc (Lsd;Laig;)V)
 *       remove(ServerPlayer)                         -> c      (desc (Laig;)V)
 *   - net.minecraft.server.level.ServerPlayerGameMode-> aih
 *       destroyBlock(BlockPos)                       -> a      (desc (Lgu;)Z)
 *       字段 player -> d(protected final)、level -> c(protected)
 *   - net.minecraft.commands.Commands                -> dt
 *       performPrefixedCommand(CommandSourceStack,String) -> a  (desc (Lds;Ljava/lang/String;)I;
 *       控制台/RCON 命令漏斗;mojmap 的 performCommand(CommandSourceStack,String)
 *       已被 ProGuard 内联,运行时不存在。游戏内聊天命令不走此方法(独立
 *       反汇编证实走 dt.a(ParseResults,String) = performCommand(ParseResults,
 *       String) 路径),暂未 hook,留待后续)
 *   - net.minecraft.server.MinecraftServer           -> 未混淆
 *       tickServer(BooleanSupplier)                  -> a      (desc (Ljava/util/function/BooleanSupplier;)V)
 *       getTickCount()                               -> ag
 *   - 反射用:Entity.getId() -> bfj.af()、BlockPos.asLong() -> gu.a()、
 *     BlockPos.getX/Y/Z(long) -> gu.a/b/c(J)、Level.getBlockState(BlockPos) ->
 *     cmm.a_(Lgu;)(aif 继承)、Block.getId(BlockState) -> cpn.i(Ldcb;)
 *
 * 载荷(小端 LE,与 include/mosaic/events.h 一致):
 *   player_join/player_leave/tick = 4B u32;block_break = 20B(player_id/x/y/z/block_type)。
 */
public final class MosaicHooks {

    private MosaicHooks() {}

    private static long rt;                                     /* 0 = 未打开,全部 no-op */
    private static final String[] EVENTS = {
        "player_join", "player_leave", "block_break", "tick", "server_command"
    };
    private static final int[] EV_IDS = new int[EVENTS.length];
    private static final long[] EV_EXEC = new long[EVENTS.length];   /* dispatch 返回执行数累积 */
    private static final long[] EV_CALLS = new long[EVENTS.length];  /* 派发调用次数 */

    /* ---- 1.20.1 混淆名反射句柄(premain 阶段服务端类尚未加载,
       故惰性解析:首次 hook 调用时解析,失败自动重试——首 tick 必然成功) ---- */
    private static Method M_ID;            /* aig.getId() (继承 bfj.af()) */
    private static Field  F_PLAYER;        /* aih.d (protected final) */
    private static Field  F_LEVEL;         /* aih.c (protected) */
    private static Method M_BLOCK_STATE;   /* aif.a_(Lgu;)Ldcb; (继承 cmm) */
    private static Method M_AS_LONG;       /* gu.a()J */
    private static Method M_GET_X, M_GET_Y, M_GET_Z;  /* gu.a/b/c(J)I */
    private static Method M_BLOCK_ID;      /* cpn.i(Ldcb;)I */
    private static Method M_TICK_COUNT;    /* MinecraftServer.ag()I */
    private static boolean reflected = false;
    private static boolean resolveWarned = false;

    public static void init(long handle) {
        rt = handle;
        for (int i = 0; i < EVENTS.length; i++)
            EV_IDS[i] = Bridge.eventId(rt, EVENTS[i]);
    }

    /* 服务端类由 bundler 自建类加载器定义——Class.forName 必须带该加载器
       (MosaicTransformer 在首次转换时捕获);退回系统加载器兜底。 */
    private static Class<?> clazz(String name) throws ClassNotFoundException {
        ClassLoader cl = com.mosaic.agent.MosaicTransformer.serverLoader();
        if (cl != null) return Class.forName(name, false, cl);
        return Class.forName(name);
    }

    private static void resolve() {
        if (reflected) return;
        try {
            M_ID = clazz("aig").getMethod("af");
            Class<?> aih = clazz("aih");
            F_PLAYER = aih.getDeclaredField("d"); F_PLAYER.setAccessible(true);
            F_LEVEL = aih.getDeclaredField("c");  F_LEVEL.setAccessible(true);
            Class<?> gu = clazz("gu");
            M_BLOCK_STATE = clazz("aif").getMethod("a_", gu);
            M_AS_LONG = gu.getMethod("a");
            M_GET_X = gu.getMethod("a", long.class);
            M_GET_Y = gu.getMethod("b", long.class);
            M_GET_Z = gu.getMethod("c", long.class);
            M_BLOCK_ID = clazz("cpn").getMethod("i", clazz("dcb"));
            M_TICK_COUNT = clazz("net.minecraft.server.MinecraftServer").getMethod("ag");
            reflected = true;
            System.out.println("Mosaic agent: hook reflection ready");
        } catch (Throwable t) {
            if (!resolveWarned) {
                resolveWarned = true;
                System.out.println("Mosaic agent: WARN reflection resolve deferred (retry on next hook): " + t);
            }
        }
    }

    /* ---- 注入 hook:player_join / player_leave ---- */
    public static void onPlayerJoin(Object p) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[4];
            putIntLE(b, 0, (Integer) M_ID.invoke(p));
            dispatch(0, b);
        } catch (Throwable t) { logErr("onPlayerJoin", t); }
    }

    public static void onPlayerLeave(Object p) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[4];
            putIntLE(b, 0, (Integer) M_ID.invoke(p));
            dispatch(1, b);
        } catch (Throwable t) { logErr("onPlayerLeave", t); }
    }

    /* ---- 注入 hook:block_break(destroyBlock 方法入口注入,方块尚未破坏) ---- */
    public static void onBlockBreak(Object gm, Object pos) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            Object player = F_PLAYER.get(gm);
            Object level = F_LEVEL.get(gm);
            Object state = M_BLOCK_STATE.invoke(level, pos);   /* 破坏前状态 */
            int blockType = (Integer) M_BLOCK_ID.invoke(null, state);
            long packed = (Long) M_AS_LONG.invoke(pos);
            byte[] b = new byte[20];
            putIntLE(b, 0, (Integer) M_ID.invoke(player));
            putIntLE(b, 4, (Integer) M_GET_X.invoke(null, packed));
            putIntLE(b, 8, (Integer) M_GET_Y.invoke(null, packed));
            putIntLE(b, 12, (Integer) M_GET_Z.invoke(null, packed));
            putIntLE(b, 16, blockType);
            dispatch(2, b);
        } catch (Throwable t) { logErr("onBlockBreak", t); }
    }

    /* ---- 注入 hook:命令(控制台/RCON 漏斗入口;"/mosaic" 前缀消费,返回 true;
       游戏内聊天命令不走此点(见头注释),暂未 hook) ---- */
    public static boolean onCommand(Object src, String cmd) {
        try {
            /* "/mosaic" 后必须跟空白或结尾,避免误消费 "/mosaictest" 等前缀命令 */
            if (cmd == null || (!cmd.equals("/mosaic") && !cmd.startsWith("/mosaic "))) return false;
            handleMosaic(cmd);
            return true;
        } catch (Throwable t) {
            logErr("onCommand", t);
            return true;   /* 已判定为 /mosaic,消费避免触发"未知命令" */
        }
    }

    /* ---- 注入 hook:服务端 tick(每 tick 派发,计数在 /mosaic status 显示) ---- */
    public static void onServerTick(Object server) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[4];
            putIntLE(b, 0, (Integer) M_TICK_COUNT.invoke(server));
            dispatch(3, b);
        } catch (Throwable t) { logErr("onServerTick", t); }
    }

    /* ---- /mosaic status | test <event> [payload_int...] ---- */
    private static void handleMosaic(String cmd) {
        String[] parts = cmd.split("\\s+");
        if (parts.length < 2) { usage(); return; }
        String sub = parts[1];
        if (sub.equals("status")) {
            resolve();
            System.out.println("Mosaic agent: status functions=" + Bridge.functionCount(rt)
                    + " working_set=" + Bridge.workingSetCount(rt)
                    + " last_error=" + Bridge.lastError(rt)
                    + " hooks_reflected=" + reflected);
            for (int i = 0; i < EVENTS.length; i++) {
                System.out.println("Mosaic agent:   " + EVENTS[i]
                        + " event_id=" + EV_IDS[i]
                        + " calls=" + EV_CALLS[i]
                        + " executed=" + EV_EXEC[i]);
            }
        } else if (sub.equals("test")) {
            if (parts.length < 3) { usage(); return; }
            String ev = parts[2];
            int idx = -1;
            for (int i = 0; i < EVENTS.length; i++)
                if (EVENTS[i].equals(ev)) { idx = i; break; }
            if (idx < 0 || EV_IDS[idx] < 0) {
                System.out.println("Mosaic agent: test: unknown or unregistered event \"" + ev + "\"");
                return;
            }
            int[] vals = new int[parts.length - 3];
            try {
                for (int i = 3; i < parts.length; i++) vals[i - 3] = Integer.parseInt(parts[i]);
            } catch (NumberFormatException e) {
                System.out.println("Mosaic agent: test: non-integer payload arg");
                return;
            }
            byte[] b = payloadFor(idx, vals);
            if (b == null) { System.out.println("Mosaic agent: test: bad payload for " + ev); return; }
            int n = Bridge.eventDispatch(rt, EV_IDS[idx], b);
            if (n > 0) EV_EXEC[idx] += n;
            EV_CALLS[idx]++;
            System.out.println("Mosaic agent: test dispatch " + ev + " -> executed=" + n);
        } else {
            usage();
        }
    }

    private static void usage() {
        System.out.println("Mosaic agent: usage: /mosaic status | /mosaic test <event> [payload_int...]");
    }

    /* 事件索引 → 载荷(小端;长度 = include/mosaic/events.h 结构体大小) */
    private static byte[] payloadFor(int idx, int[] vals) {
        switch (EVENTS[idx]) {
            case "server_command":
                return new byte[0];                     /* 无载荷 */
            case "tick": case "player_join": case "player_leave": {
                byte[] b = new byte[4];                 /* u32 */
                putIntLE(b, 0, vals.length > 0 ? vals[0] : 0);
                return b;
            }
            case "block_break": {                       /* player_id/x/y/z/block_type = 5×u32 */
                byte[] b = new byte[20];
                for (int i = 0; i < 5; i++)
                    putIntLE(b, i * 4, vals.length > i ? vals[i] : 0);
                return b;
            }
            default:
                return null;
        }
    }

    /* 派发:事件未注册(-1)→ 跳过;返回执行数累积到静态计数器 */
    private static void dispatch(int idx, byte[] payload) {
        if (EV_IDS[idx] < 0) return;
        int n = Bridge.eventDispatch(rt, EV_IDS[idx], payload);
        if (n > 0) EV_EXEC[idx] += n;
        EV_CALLS[idx]++;
    }

    private static void putIntLE(byte[] b, int off, int v) {
        b[off] = (byte) (v & 0xFF);
        b[off + 1] = (byte) ((v >>> 8) & 0xFF);
        b[off + 2] = (byte) ((v >>> 16) & 0xFF);
        b[off + 3] = (byte) ((v >>> 24) & 0xFF);
    }

    private static void logErr(String what, Throwable t) {
        System.out.println("Mosaic agent: hook error " + what + ": " + t);
    }
}
