package com.mosaic.agent;

import mosaic.Bridge;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * M4-2:注入目标的静态 hook 集合(由 MosaicTransformer 在 vanilla 1.20.1 服务端
 * 方法内插入静态调用;所有方法 try-catch,注入代码不得崩服务端)。
 *
 * 类签名核实(2026-08-22 + 2026-08-23 M8-D,官方 server.jar 1.20.1 的
 * ProGuard 混淆名,经 Mojang server_mappings(server.txt)+ javap 逐项确认;
 * Mojang 自 1.20.1 起对服务端 jar 做 ProGuard 混淆,mojmap 名在运行时不存在):
 *   - net.minecraft.server.players.PlayerList        -> alk
 *       placeNewPlayer(Connection,ServerPlayer)      -> a      (desc (Lsd;Laig;)V)
 *       remove(ServerPlayer)                         -> c      (desc (Laig;)V)
 *   - net.minecraft.server.level.ServerPlayerGameMode-> aih
 *       destroyBlock(BlockPos)                       -> a      (desc (Lgu;)Z)
 *       字段 player -> d(protected final)、level -> c(protected)
 *   - net.minecraft.commands.Commands                -> dt
 *       performPrefixedCommand(CommandSourceStack,String) -> a  (desc (Lds;Ljava/lang/String;)I;
 *       控制台/RCON 命令漏斗;mojmap 的 performCommand(CommandSourceStack,String)
 *       已被 ProGuard 内联,运行时不存在)
 *       performCommand(ParseResults,String)          -> a  (desc (Lcom/mojang/brigadier/ParseResults;Ljava/lang/String;)I;
 *       M8-D:游戏内聊天命令漏斗。aiy.performChatCommand 反汇编证实:
 *       CommandDispatcher.parse → dt.a(ParseResults,UnaryOperator)(继续) →
 *       dt.a(ParseResults,String) 执行,返回结果被调用方 pop 丢弃。brigadier
 *       未混淆但由 bundler 加载器加载 → 经 serverLoader 反射取
 *       ParseResults.getContext().getSource() = CommandSourceStack(ds))
 *   - net.minecraft.world.item.BlockItem             -> cds
 *       placeBlock(BlockPlaceContext,BlockState)     -> a  (desc (Lcih;Ldcb;)Z;
 *       M8-D:block_place 注入点——1.20.1 放置成功路径:place() 校验通过后
 *       才调用 placeBlock,入参 state = 实际放置的方块状态(比 useItemOn
 *       入口更精准:useItemOn 对任何右键交互触发,且拿不到放置后状态))
 *   - net.minecraft.server.level.ServerLevel        -> aif
 *       addFreshEntity(Entity)                       -> b  (desc (Lbfj;)Z;
 *       M8-D:服务端实体生成统一漏斗——反汇编证实 addFreshEntity → addEntity
 *       (j);Level.addFreshEntityWithPassengers(实体带乘客)调 addFreshEntity)
 *   - net.minecraft.server.network.ServerGamePacketListenerImpl -> aiy
 *       handleChat(ServerboundChatPacket)            -> a  (desc (Lzi;)V;
 *       M8-D:player_chat 注入点——非 "/" 聊天包入口;"/" 命令走 zh 包
 *       (handleChatCommand → performChatCommand → dt.a(ParseResults,String)),
 *       不触发本 hook,与 player_chat/player_command 语义划分一致)
 *       字段 player -> b(net.minecraft.server.level.ServerPlayer)
 *   - net.minecraft.server.level.ServerPlayer       -> aig
 *       die(DamageSource)                            -> a  (desc (Lben;)V;
 *       M8-D:player_death 注入点;另有 actuallyHurt = a(Lben;F)Z 区分)
 *   - net.minecraft.server.MinecraftServer           -> 未混淆
 *       tickServer(BooleanSupplier)                  -> a      (desc (Ljava/util/function/BooleanSupplier;)V)
 *       getTickCount()                               -> ag
 *   - 反射用(M8-D 新增已核实;2026-08-23 M8-D 评审修正):Entity.getId() ->
 *     bfj.af()、Entity.getX/Y/Z() -> bfj.dn/dp/dt()、BlockPos.asLong() ->
 *     gu.a()、BlockPos.getX/Y/Z(long) -> gu.a/b/c(J)、
 *     Level.getBlockState(BlockPos) -> cmm.a_(Lgu;)(aif 继承)、
 *     Block.getId(BlockState) -> cpn.i(Ldcb;)、UseOnContext(BlockPlaceContext
 *     父类)getPlayer/getClickedPos -> cij.o()/cij.a()、
 *     ParseResults.getContext() -> CommandContextBuilder、
 *     CommandContextBuilder.getSource()(brigadier 未混淆,javap 实测)。
 *     [修正] 1.20.1 EntityType 无 getId():bfn.a() 实为
 *     a()Ljava/lang/Class; = mojmap getBaseClass()(javap 实测;此前误作
 *     EntityType.getId)——注册 id 需 BuiltInRegistries.ENTITY_TYPE 访问
 *     (混淆名待后续核实),entity_type 载荷暂为 0(记录为后续项)
 *
 * 载荷(小端 LE,与 include/mosaic/events.h 一致):
 *   player_join/player_leave/tick/player_chat/player_death = 4B u32;
 *   block_break/block_place/entity_spawn = 20B(5×u32)。
 */
public final class MosaicHooks {

    private MosaicHooks() {}

    private static long rt;                                     /* 0 = 未打开,全部 no-op */
    private static final String[] EVENTS = {
        "player_join", "player_leave", "block_break", "tick", "server_command",
        "block_place", "entity_spawn", "player_chat", "player_death",
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
    /* M8-D:entity_spawn 载荷(1.20.1 EntityType 无 getId();注册 id 需
       BuiltInRegistries.ENTITY_TYPE(混淆名待后续核实),
       此处 entity_type 暂为 0 —— 记录为后续项) */
    private static Method M_ENT_X, M_ENT_Y, M_ENT_Z;  /* bfj.dn/dp/dt()D */
    /* M8-D:player_chat 载荷(aiy 的 player 字段) */
    private static Field  F_AIY_PLAYER;    /* aiy.b (ServerGamePacketListenerImpl.player) */
    /* M8-D:block_place 载荷(BlockPlaceContext 继承 cij=UseOnContext) */
    private static Method M_CTX_PLAYER;    /* cij.o()Lbyo; (getPlayer,可 null) */
    private static Method M_CTX_POS;       /* cij.a()Lgu; (getClickedPos) */
    /* M8-D:chat 命令 source 提取(brigadier 未混淆,但由 bundler 加载器加载) */
    private static Method M_PARSE_CTX;     /* ParseResults.getContext() */
    private static Method M_CTX_SRC;       /* CommandContextBuilder.getSource() */
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
            /* M8-D:entity_spawn(x/y/z;entity_type 载荷暂为 0——bfn.a() 实为
               getBaseClass()Ljava/lang/Class;,1.20.1 EntityType 无 getId) */
            M_ENT_X = clazz("bfj").getMethod("dn");
            M_ENT_Y = clazz("bfj").getMethod("dp");
            M_ENT_Z = clazz("bfj").getMethod("dt");
            /* M8-D:player_chat(aiy.b 字段) */
            F_AIY_PLAYER = clazz("aiy").getDeclaredField("b");
            F_AIY_PLAYER.setAccessible(true);
            /* M8-D:block_place(cij = UseOnContext,BlockPlaceContext 父类) */
            M_CTX_PLAYER = clazz("cij").getMethod("o");
            M_CTX_POS = clazz("cij").getMethod("a");
            /* M8-D:chat 命令 source(brigadier 未混淆,按名字反射;javap 实测
               brigadier-1.1.8:ParseResults.getContext() 返回
               CommandContextBuilder(而非 CommandContext),其上有 getSource()——
               此前在 CommandContext 上查找 getSource,invoke 必然
               IllegalArgumentException 被内层 catch 吞掉) */
            M_PARSE_CTX = clazz("com.mojang.brigadier.ParseResults").getMethod("getContext");
            M_CTX_SRC = clazz("com.mojang.brigadier.context.CommandContextBuilder").getMethod("getSource");
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

    /* ---- 注入 hook:命令(控制台/RCON 漏斗入口;"/mosaic" 前缀消费,返回 true) ---- */
    public static boolean onCommand(Object src, String cmd) {
        try {
            return handleCommand(cmd);
        } catch (Throwable t) {
            logErr("onCommand", t);
            return true;   /* 已判定为 /mosaic,消费避免触发"未知命令" */
        }
    }

    /* ---- 注入 hook:游戏内聊天命令(M8-D;dt.a(ParseResults,String) =
       performCommand(ParseResults,String) 入口;signature 无 CommandSourceStack
       参数,从 ParseResults.getContext()(= CommandContextBuilder,javap 实测)
       .getSource() 反射提取;提取值当前被丢弃,但链路必须正确——feedback
       通道为后续项) ---- */
    public static boolean onChatCommand(Object results, String cmd) {
        try {
            /* 提取 source(失败不阻断:/mosaic 处理不依赖 source) */
            if (results != null && M_PARSE_CTX != null && M_CTX_SRC != null) {
                try {
                    Object ctx = M_PARSE_CTX.invoke(results);
                    if (ctx != null) M_CTX_SRC.invoke(ctx);
                } catch (Throwable t) { /* 提取失败按 null 处理 */ }
            }
            return handleCommand(cmd);
        } catch (Throwable t) {
            logErr("onChatCommand", t);
            return true;   /* 已判定为 /mosaic,消费避免"未知命令"反馈 */
        }
    }

    /* 共享命令处理:仅 "/mosaic" 前缀命令由本 agent 消费 */
    private static boolean handleCommand(String cmd) {
        /* "/mosaic" 后必须跟空白或结尾,避免误消费 "/mosaictest" 等前缀命令 */
        if (cmd == null || (!cmd.equals("/mosaic") && !cmd.startsWith("/mosaic "))) return false;
        handleMosaic(cmd);
        return true;
    }

    /* ---- 注入 hook:block_place(M8-D;BlockItem.placeBlock(BlockPlaceContext,
       BlockState) 入口——place() 校验通过后才调 placeBlock,入参 state 即
       实际放置的方块状态;ctx = BlockPlaceContext(继承 cij=UseOnContext),
       getPlayer 可能为 null(如发射器放置)→ player_id=0) ---- */
    public static void onBlockPlace(Object ctx, Object state) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            Object pos = M_CTX_POS.invoke(ctx);
            long packed = (Long) M_AS_LONG.invoke(pos);
            Object player = M_CTX_PLAYER.invoke(ctx);
            int playerId = 0;
            if (player != null) playerId = (Integer) M_ID.invoke(player);
            byte[] b = new byte[20];
            putIntLE(b, 0, playerId);
            putIntLE(b, 4, (Integer) M_GET_X.invoke(null, packed));
            putIntLE(b, 8, (Integer) M_GET_Y.invoke(null, packed));
            putIntLE(b, 12, (Integer) M_GET_Z.invoke(null, packed));
            putIntLE(b, 16, (Integer) M_BLOCK_ID.invoke(null, state));
            dispatch(5, b);
        } catch (Throwable t) { logErr("onBlockPlace", t); }
    }

    /* ---- 注入 hook:entity_spawn(M8-D;ServerLevel.addFreshEntity(Entity)
       入口——服务端实体生成统一漏斗,含带乘客路径) ---- */
    public static void onEntitySpawn(Object e) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[20];
            putIntLE(b, 0, (Integer) M_ID.invoke(e));
            /* 1.20.1 EntityType 无 getId();注册 id 需 BuiltInRegistries.ENTITY_TYPE
               (混淆名待后续核实),此处 entity_type 暂为 0 —— 记录为后续项 */
            putIntLE(b, 4, 0);
            putIntLE(b, 8, (int) Math.floor((Double) M_ENT_X.invoke(e)));
            putIntLE(b, 12, (int) Math.floor((Double) M_ENT_Y.invoke(e)));
            putIntLE(b, 16, (int) Math.floor((Double) M_ENT_Z.invoke(e)));
            dispatch(6, b);
        } catch (Throwable t) { logErr("onEntitySpawn", t); }
    }

    /* ---- 注入 hook:player_chat(M8-D;ServerGamePacketListenerImpl.handleChat
       入口——非 "/" 聊天包;player = aiy.b 字段) ---- */
    public static void onPlayerChat(Object handler) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            Object player = F_AIY_PLAYER.get(handler);
            if (player == null) return;
            byte[] b = new byte[4];
            putIntLE(b, 0, (Integer) M_ID.invoke(player));
            dispatch(7, b);
        } catch (Throwable t) { logErr("onPlayerChat", t); }
    }

    /* ---- 注入 hook:player_death(M8-D;ServerPlayer.die(DamageSource) 入口) ---- */
    public static void onPlayerDeath(Object p) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[4];
            putIntLE(b, 0, (Integer) M_ID.invoke(p));
            dispatch(8, b);
        } catch (Throwable t) { logErr("onPlayerDeath", t); }
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

    /* ---- /mosaic status | install <pack path> | test <event> [payload_int...] ---- */
    private static void handleMosaic(String cmd) {
        String[] parts = cmd.split("\\s+");
        if (parts.length < 2) { usage(); return; }
        String sub = parts[1];
        if (sub.equals("status")) {
            resolve();
            System.out.println("Mosaic agent: status functions=" + Bridge.functionCount(rt)
                    + " packs=" + Bridge.packCount(rt)
                    + " working_set=" + Bridge.workingSetCount(rt)
                    + " last_error=" + Bridge.lastError(rt)
                    + " hooks_reflected=" + reflected);
            for (int i = 0; i < EVENTS.length; i++) {
                System.out.println("Mosaic agent:   " + EVENTS[i]
                        + " event_id=" + EV_IDS[i]
                        + " calls=" + EV_CALLS[i]
                        + " executed=" + EV_EXEC[i]);
            }
        } else if (sub.equals("install")) {
            /* M4-3:世界内动态加载——运行中挂载新 pack(零重启),挂载后下个
               tick 的派发即覆盖其订阅者(dispatch 遍历 rt->packs) */
            if (parts.length < 3) { usage(); return; }
            String p = parts[2];
            long before = Bridge.functionCount(rt);
            int rc = Bridge.runtimeAddPack(rt, p);
            if (rc == 0) {
                long after = Bridge.functionCount(rt);
                System.out.println("Mosaic agent: installed " + p
                        + " (functions=" + before + "->" + after + ")");
            } else {
                System.out.println("Mosaic agent: install " + p
                        + " failed (err=" + Bridge.lastError(rt) + ")");
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
        System.out.println("Mosaic agent: usage: /mosaic status | /mosaic install <pack path> | /mosaic test <event> [payload_int...]");
    }

    /* 事件索引 → 载荷(小端;长度 = include/mosaic/events.h 结构体大小) */
    private static byte[] payloadFor(int idx, int[] vals) {
        switch (EVENTS[idx]) {
            case "server_command":
                return new byte[0];                     /* 无载荷 */
            case "tick": case "player_join": case "player_leave":
            case "player_chat": case "player_death": {  /* u32 */
                byte[] b = new byte[4];
                putIntLE(b, 0, vals.length > 0 ? vals[0] : 0);
                return b;
            }
            case "block_break": case "block_place":     /* player_id/x/y/z/block_type = 5×u32 */
            case "entity_spawn": {                      /* entity_id/entity_type/x/y/z = 5×u32 */
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
