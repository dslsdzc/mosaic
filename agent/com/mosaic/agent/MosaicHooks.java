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
 *       [Task 5] 5.4:注入改为返回值出口钩子(onBlockPlaceResult):方法所有
 *       IRETURN 前 dup 返回值 + ALOAD 入口存入新局部槽的 ctx/state → 钩子
 *       收到真实返回值;返回 false(放置失败)不派发 block_place(语义与
 *       block_break 破坏前状态一致;文档注明)。
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
 *       [Task 5] 5.5:钩子入参追加包对象(packet = 方法第 2 参 local 1),
 *       从 ServerboundChatPacket.message = zi.a 提取消息文本(Java 侧暴露)。
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
 *     EntityType.getId)——注册 id 需 BuiltInRegistries.ENTITY_TYPE 访问。
 *   - [Task 6 新增,2026-08-23 server_mappings(0b4dba049482…) + javap 实测
 *     server-1.20.1.jar 核实,记录见 .superpowers/sdd/task-6-report.md]:
 *     net.minecraft.network.Connection -> sd
 *       入站:channelRead0(ChannelHandlerContext,Object)(SimpleChannelInbound
 *       Handler 桥接入口,包解码后;netty channelRead 仅对真实包调用本方法)
 *       -> 保持名 channelRead0(protected),desc
 *       (Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V;
 *       同类的 typed 版 a(ChannelHandlerContext,Packet) desc
 *       (Lio/netty/channel/ChannelHandlerContext;Luo;)V = mojmap
 *       channelRead0(ChannelHandlerContext,Packet)(server.txt 158:171,javap
 *       双证——桥与 typed 并存,netty 只调桥,桥 cast 后调 typed)
 *       出站:doSendPacket(Packet,PacketSendListener,ConnectionProtocol,
 *       ConnectionProtocol) -> a(private),desc (Luo;Lsl;Lse;Lse;)V
 *       ——1.20.1 发送漏斗(包编码前出口)。[偏差] brief 的
 *       channelWrite(ChannelHandlerContext,Object,ChannelPromise) 不存在于
 *       1.20.1 Connection:javap 实测 sd 仅 extends SimpleChannelInboundHandler
 *       (无 ChannelOutboundHandler 实现、无 channelWrite 方法)——以 doSendPacket
 *       替代,语义同(brief 文档注明"包编码前出口")。
 *       [Task 1] 出站派发已迁离 doSendPacket → PacketEncoder.encode 出口
 *       (真实字节长度;防双计——旧挂钩已移除,每包恰好派发一次)。
 *       字段 packetListener -> o(private,类型 sk = net.minecraft.network.PacketListener)
 *     net.minecraft.network.protocol.Packet -> uo(接口)、
 *     PacketListener -> sk、PacketSendListener -> sl、
 *     ConnectionProtocol -> se(全部 javap 实测)
 *   - [Task 1 新增,2026-08-23 server_mappings(同源 server.txt)+ javap -p -s
 *     实测本地 jar 核实,记录见 .superpowers/sdd/task-1-report.md]:
 *     net.minecraft.network.PacketDecoder -> si(extends ByteToMessageDecoder)
 *       decode(ChannelHandlerContext,ByteBuf,List) -> 保持名 decode(protected),
 *       desc (Lio/netty/channel/ChannelHandlerContext;Lio/netty/buffer/ByteBuf;
 *       Ljava/util/List;)V——分帧器(splitter)切好整包后每包恰好调用一次,
 *       入口 buf.readableBytes() = 包字节数(不含分帧器 VarInt 长度前缀;
 *       压缩开启 = 压缩后字节数 + size 前缀;javap -c 实测 decode 入口先读
 *       readableBytes 并整帧消费)
 *     net.minecraft.network.PacketEncoder -> sj(extends
 *       MessageToByteEncoder<uo<?>>)
 *       encode(ChannelHandlerContext,Packet,ByteBuf) -> a(protected),
 *       desc (Lio/netty/channel/ChannelHandlerContext;Luo;Lio/netty/buffer/ByteBuf;)V
 *       ——真实编码方法(同类的 encode(ChannelHandlerContext,Object,ByteBuf)
 *       为泛型擦除桥:MessageToByteEncoder.write → 桥 → a,每包恰好一次;
 *       javap -c 实测 a 仅一个 RETURN(正常路径),其余出口为 ATHROW——编码
 *       失败不派发 packet_sent);出口 out.writerIndex() = 编码后字节数
 *       (不含 prepender 追加的 VarInt 长度前缀)
 *     netty 4.1.82.Final(未混淆,经 serverLoader 加载;javap 实测):
 *       AttributeKey.valueOf(String) 静态、AttributeMap.attr(AttributeKey)
 *       → Attribute、Attribute.get()/set(T)(擦除 Object)、
 *       ByteBuf.readableBytes()/writerIndex()、
 *       ChannelHandlerContext.channel() → Channel、
 *       Channel.pipeline() → ChannelPipeline、ChannelPipeline.get(Class)
 *     pipeline 结构(configureSerialization 反汇编,sd.a(ChannelPipeline,up)):
 *       addLast 依次 splitter(sp)/decoder(si)/prepender(sq)/encoder(sj)/
 *       unbundler(sh)/bundler(sg);Connection(sd) 在构造期 addLast
 *       "packet_handler" 先入——encode 挂钩经 ctx.channel().pipeline()
 *       .get(sd.class) 取 Connection 实例(不依赖 handler 名)
 *   - [Task 5 新增,2026-08-23 server_mappings(0b4dba04…) + javap 实测
 *     server-1.20.1.jar 核实,记录见 .superpowers/sdd/task-5-report.md]:
 *     BuiltInRegistries -> jb、ENTITY_TYPE 字段 -> h(public static final
 *     gz<bfn<?>>)、Registry.getId(Object) -> hr.a(T)(public abstract int
 *     a(T))、Entity.getType() -> bfj.ae()(public bfn<?> ae())、
 *     Entity.level() -> bfj.dI()(public cmm dI())、
 *     Level.dimension() -> cmm.ac()(public acp<cmm> ac())、
 *     ResourceKey.location() -> acp.a()(public acq a())、
 *     ServerboundChatPacket.message -> zi.a(private final String,
 *     accessor a())、CommandSourceStack.getPlayer() -> ds.i()(public aig
 *     i())。source 字段:1.20.1 addFreshEntity 入口钩子点不可得生成来源,
 *     载荷固定 0(注释注明)。
 *
 * 载荷(小端 LE,与 include/mosaic/events.h 一致):
 *   player_join/player_leave/tick/player_chat/player_death = 4B u32;
 *   player_command = 8B(player_id + cmd_hash 2×u32);
 *   block_break/block_place = 20B(5×u32);
 *   entity_spawn = 28B(entity_id/entity_type/x/y/z/dimension/source 7×u32);
 *   packet_received/packet_sent = 12B(3×u32:player_id/packet_id/size_hint;
 *   size_hint = 真实编码/解码字节长度(Task 1 实测;不可得 → 0):入站 =
 *   PacketDecoder.decode 入口 buf.readableBytes()(分帧器切好整包)、出站 =
 *   PacketEncoder.encode 出口 out.writerIndex())。
 */
public final class MosaicHooks {

    private MosaicHooks() {}

    /* volatile:注入 hook 在 Netty 线程/服务端线程交叉调用,rt 在 premain
       (MosaicAgent 线程)初始化——跨线程可见性由 volatile 保证,避免 hook
       线程看到过期值 0 而静默 no-op(仅日志可察觉) */
    private static volatile long rt;                             /* 0 = 未打开,全部 no-op */
    private static final int MOSAIC_ERR_TIMEOUT = 8;             /* include/mosaic/base.h 枚举,只增不减 */
    private static final String[] EVENTS = {
        "player_join", "player_leave", "block_break", "tick", "server_command",
        "block_place", "entity_spawn", "player_chat", "player_death",
        "player_command",   /* Task 5 5.3:chat 命令漏斗(onChatCommand)派发 */
        "packet_received", "packet_sent",   /* Task 6:网络域(onPacketReceived/onPacketSent) */
    };
    private static final int[] EV_IDS = new int[EVENTS.length];
    private static final long[] EV_EXEC = new long[EVENTS.length];   /* dispatch 返回执行数累积 */
    private static final long[] EV_CALLS = new long[EVENTS.length];  /* 派发调用次数 */
    /* 派发处下标常量:init 时按事件名解析(未注册 → -1),不硬编码 EVENTS 数组
       位置——避免事件名与下标耦合漂移(F-9:dispatch(9/10/11,...) 魔数消除) */
    private static int EV_PLAYER_CMD = -1;
    private static int EV_PACKET_RECV = -1;
    private static int EV_PACKET_SENT = -1;

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
    /* M8-D:entity_spawn 载荷(1.20.1 EntityType 无 getId()——注册 id 经
       BuiltInRegistries.ENTITY_TYPE = jb.h、Registry.getId = hr.a(T),
       Task 5 5.1/5.2 已核实并实现) */
    private static Method M_ENT_X, M_ENT_Y, M_ENT_Z;  /* bfj.dn/dp/dt()D */
    private static Method M_ENT_TYPE;     /* 5.1:bfj.ae()=Entity.getType() */
    private static Field  F_REG_ENTITY_TYPE;   /* 5.1:jb.h=BuiltInRegistries.ENTITY_TYPE */
    private static Method M_REG_ENTITY_ID;     /* 5.1:hr.a(T)=Registry.getId(Object) */
    private static Method M_ENT_LEVEL;    /* 5.2:bfj.dI()=Entity.level() */
    private static Method M_LEVEL_DIM;    /* 5.2:cmm.ac()=Level.dimension() */
    private static Method M_RESKEY_LOC;   /* 5.2:acp.a()=ResourceKey.location() */
    /* M8-D:player_chat 载荷(aiy 的 player 字段) */
    private static Field  F_AIY_PLAYER;    /* aiy.b (ServerGamePacketListenerImpl.player) */
    private static Method M_CHAT_MSG;      /* 5.5:zi.a()=ServerboundChatPacket.message() */
    /* M8-D:block_place 载荷(BlockPlaceContext 继承 cij=UseOnContext) */
    private static Method M_CTX_PLAYER;    /* cij.o()Lbyo; (getPlayer,可 null) */
    private static Method M_CTX_POS;       /* cij.a()Lgu; (getClickedPos) */
    /* M8-D:chat 命令 source 提取(brigadier 未混淆,但由 bundler 加载器加载) */
    private static Method M_PARSE_CTX;     /* ParseResults.getContext() */
    private static Method M_CTX_SRC;       /* CommandContextBuilder.getSource() */
    private static Method M_SRC_PLAYER;    /* 5.3:ds.i()=CommandSourceStack.getPlayer() */
    /* Task 6:网络域(Connection 双向挂钩;sd = Connection,核实见头注释) */
    private static Class<?> C_AIY;         /* aiy = ServerGamePacketListenerImpl */
    private static Field  F_PACKET_LISTENER; /* sd.o (private, Connection.packetListener) */
    /* Task 1:netty 反射句柄(未混淆,经 serverLoader 加载;javap 实测核实,
       见头注释)——跨挂钩大小传递(Channel.attr)与出站 player 提取 */
    private static Class<?> C_CONN;         /* sd = Connection(pipeline.get 用) */
    private static Method M_CTX_CHANNEL;    /* ChannelHandlerContext.channel() */
    private static Method M_CHANNEL_PIPE;   /* Channel.pipeline() */
    private static Method M_PIPE_GET;       /* ChannelPipeline.get(Class) */
    private static Method M_ATTR;           /* AttributeMap.attr(AttributeKey) */
    private static Method M_ATTR_GET;       /* Attribute.get() */
    private static Method M_ATTR_SET;       /* Attribute.set(Object) */
    private static Method M_ATTRKEY_VALUEOF; /* AttributeKey.valueOf(String) */
    private static Method M_BUF_READABLE;   /* ByteBuf.readableBytes() */
    private static Method M_BUF_WRITER;     /* ByteBuf.writerIndex() */
    private static Object ATTR_SIZE_KEY;    /* AttributeKey "mosaic_packet_size"(入站) */
    private static Object ATTR_ENC_KEY;     /* AttributeKey "mosaic_enc_buf"(出站) */
    private static boolean reflected = false;
    private static boolean resolveWarned = false;

    /* 5.5:反馈通道——最近 chat 消息文本与 chat 命令 source
       (CommandSourceStack,ds)暴露给 Java 处理方(静态 accessor,供回复玩家;
       无客户端时仅 /mosaic test 与日志可达,真实值需客户端触发) */
    private static volatile String chatMessage;
    private static volatile Object chatSource;
    public static String chatMessage() { return chatMessage; }
    public static Object chatSource() { return chatSource; }

    public static void init(long handle) {
        rt = handle;
        /* M9:每事件派发超时预算 200ms(0 = 不限制)。慢机世界生成不触发
           (单次派发微秒级);若某订阅者卡死 > 200ms,后续订阅者被跳过、
           lastError == MOSAIC_ERR_TIMEOUT,派发后打印告警(仅日志)。
           预算只保护"慢函数不阻塞同事件其他订阅者",不能中断正在执行的函数。 */
        Bridge.setDispatchTimeout(rt, 200_000);
        for (int i = 0; i < EVENTS.length; i++)
            EV_IDS[i] = Bridge.eventId(rt, EVENTS[i]);
        EV_PLAYER_CMD = eventIndex("player_command");
        EV_PACKET_RECV = eventIndex("packet_received");
        EV_PACKET_SENT = eventIndex("packet_sent");
    }

    /* 事件名 → EVENTS 数组下标(未注册/未找到 → -1) */
    private static int eventIndex(String name) {
        for (int i = 0; i < EVENTS.length; i++)
            if (EVENTS[i].equals(name)) return i;
        return -1;
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
            /* Task 5 5.1/5.2:entity_spawn 真实载荷(混淆名经 server_mappings
               + javap 实测核实,记录见 MosaicHooks 头注释)
               - bfj.ae() = Entity.getType() → EntityType(bfn)
               - jb.h = BuiltInRegistries.ENTITY_TYPE(静态字段,public)
               - hr.a(T) = Registry.getId(Object) → 注册 id(1.20.1 EntityType
                 无 getId();bfn.a() 实为 getBaseClass())
               - bfj.dI() = Entity.level()、cmm.ac() = Level.dimension()、
                 acp.a() = ResourceKey.location() → 维度 location 串 */
            M_ENT_X = clazz("bfj").getMethod("dn");
            M_ENT_Y = clazz("bfj").getMethod("dp");
            M_ENT_Z = clazz("bfj").getMethod("dt");
            M_ENT_TYPE = clazz("bfj").getMethod("ae");
            F_REG_ENTITY_TYPE = clazz("jb").getField("h");
            M_REG_ENTITY_ID = clazz("hr").getMethod("a", Object.class);
            M_ENT_LEVEL = clazz("bfj").getMethod("dI");
            M_LEVEL_DIM = clazz("cmm").getMethod("ac");
            M_RESKEY_LOC = clazz("acp").getMethod("a");
            /* M8-D:player_chat(aiy.b 字段)+ 5.5:消息文本(zi.a() accessor,
               ServerboundChatPacket.message) */
            F_AIY_PLAYER = clazz("aiy").getDeclaredField("b");
            F_AIY_PLAYER.setAccessible(true);
            M_CHAT_MSG = clazz("zi").getMethod("a");
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
            /* 5.3:player_command 载荷 player_id —— ds.i() = getPlayer()
               (ServerPlayer,可 null → player_id=0) */
            M_SRC_PLAYER = clazz("ds").getMethod("i");
            /* Task 6:网络域——sd.o(packetListener, private)+ aiy 类(玩家
               提取:listener instanceof aiy → 读 aiy.b 字段;登录/状态/握手
               阶段 listener 非 aiy → player_id=0) */
            C_AIY = clazz("aiy");
            F_PACKET_LISTENER = clazz("sd").getDeclaredField("o");
            F_PACKET_LISTENER.setAccessible(true);
            /* Task 1:netty 句柄(未混淆;服务器打包 4.1.82.Final,javap 实测
               方法签名,见头注释)——AttributeKey.valueOf(String) 静态;
               AttributeMap.attr(AttributeKey);Attribute.get/set(Object);
               ByteBuf.readableBytes/writerIndex;ChannelHandlerContext.channel;
               Channel.pipeline;ChannelPipeline.get(Class)。两把 key 由 valueOf
               按名取单例(同名同实例),同 MosaicHooks 静态字段 → 注入 hook
               与 channelRead0 hook 共用同一 AttributeKey。 */
            C_CONN = clazz("sd");
            M_CTX_CHANNEL = clazz("io.netty.channel.ChannelHandlerContext").getMethod("channel");
            M_CHANNEL_PIPE = clazz("io.netty.channel.Channel").getMethod("pipeline");
            M_PIPE_GET = clazz("io.netty.channel.ChannelPipeline").getMethod("get", Class.class);
            M_ATTR = clazz("io.netty.util.AttributeMap").getMethod("attr",
                    clazz("io.netty.util.AttributeKey"));
            M_ATTR_GET = clazz("io.netty.util.Attribute").getMethod("get");
            M_ATTR_SET = clazz("io.netty.util.Attribute").getMethod("set", Object.class);
            M_ATTRKEY_VALUEOF = clazz("io.netty.util.AttributeKey").getMethod("valueOf", String.class);
            M_BUF_READABLE = clazz("io.netty.buffer.ByteBuf").getMethod("readableBytes");
            M_BUF_WRITER = clazz("io.netty.buffer.ByteBuf").getMethod("writerIndex");
            ATTR_SIZE_KEY = M_ATTRKEY_VALUEOF.invoke(null, "mosaic_packet_size");
            ATTR_ENC_KEY = M_ATTRKEY_VALUEOF.invoke(null, "mosaic_enc_buf");
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
       .getSource() 反射提取;提取值经局部变量直传 dispatchPlayerCommand
       (不再经共享静态字段往返——见下方 source 注释),同时存入 chatSource
       (5.5 反馈通道,静态 accessor 暴露给 Java 处理方)。
       Task 5 5.3:非 /mosaic 命令 → 派发 player_command(player_id +
       FNV-1a-32(命令文本去前导 '/'))——chat 命令漏斗) ---- */
    public static boolean onChatCommand(Object results, String cmd) {
        try {
            /* 提取 source(失败不阻断:/mosaic 处理不依赖 source)。提取失败 →
               本地变量保持 null 直传 dispatchPlayerCommand——不写共享字段,
               避免"保留上一个命令的 source"与多连接并发竞态(F-1;chatSource
               字段仅剩 5.5 反馈通道用途) */
            Object source = null;
            if (results != null && M_PARSE_CTX != null && M_CTX_SRC != null) {
                try {
                    Object ctx = M_PARSE_CTX.invoke(results);
                    if (ctx != null) {
                        source = M_CTX_SRC.invoke(ctx);
                        chatSource = source;   /* 反馈通道(仅观测/回复用) */
                    }
                } catch (Throwable t) { /* 提取失败按 null 处理 */ }
            }
            if (handleCommand(cmd)) return true;
            if (rt != 0 && reflected) dispatchPlayerCommand(cmd, source);
            return false;
        } catch (Throwable t) {
            logErr("onChatCommand", t);
            return true;   /* 已判定为 /mosaic,消费避免"未知命令"反馈 */
        }
    }

    /* 5.3:player_command 派发(chat 命令漏斗内非 /mosaic 命令)。
       载荷 8B = mosaic_ev_player_command { player_id; cmd_hash }:
       player_id 取 CommandSourceStack.getPlayer()(ds.i())的实体 id,非玩家
       source → 0;cmd_hash = FNV-1a-32(命令文本去前导 '/')。
       source 由 onChatCommand 提取并传参(提取失败 → null;不再经共享静态
       字段往返——单线程下避免陈旧值、多连接下避免竞态)。 */
    private static void dispatchPlayerCommand(String cmd, Object source) {
        try {
            if (EV_IDS[EV_PLAYER_CMD] < 0) return;      /* 事件未注册 → 跳过 */
            int playerId = 0;
            if (source != null && M_SRC_PLAYER != null) {
                try {
                    Object p = M_SRC_PLAYER.invoke(source);
                    if (p != null) playerId = (Integer) M_ID.invoke(p);
                } catch (Throwable t) { /* player 提取失败 → 0 */ }
            }
            String body = cmd.startsWith("/") ? cmd.substring(1) : cmd;
            byte[] b = new byte[8];
            putIntLE(b, 0, playerId);
            putIntLE(b, 4, fnv1a32(body));
            dispatch(EV_PLAYER_CMD, b);
        } catch (Throwable t) { logErr("dispatchPlayerCommand", t); }
    }

    /* 共享命令处理:仅 "/mosaic" 前缀命令由本 agent 消费 */
    private static boolean handleCommand(String cmd) {
        /* "/mosaic" 后必须跟空白或结尾,避免误消费 "/mosaictest" 等前缀命令 */
        if (cmd == null || (!cmd.equals("/mosaic") && !cmd.startsWith("/mosaic "))) return false;
        handleMosaic(cmd);
        return true;
    }

    /* ---- 注入 hook:block_place(Task 5 5.4;BlockItem.placeBlock
       (BlockPlaceContext,BlockState) 返回值出口钩子——transformer 在方法所有
       IRETURN 前 dup 返回值并 ALOAD 入口存入新局部槽的 ctx/state 后调用
       本钩子,ok = placeBlock 真实返回值:返回 false(放置失败)不派发
       block_place(语义与 block_break 破坏前状态一致,文档注明;放置成功
       过滤 = M8-D 遗留项)。
       ctx = BlockPlaceContext(继承 cij=UseOnContext),getPlayer 可能为 null
       (如发射器放置)→ player_id=0) ---- */
    public static void onBlockPlaceResult(boolean ok, Object ctx, Object state) {
        try {
            if (rt == 0) return;
            if (!ok) return;              /* 放置失败 → 不派发 */
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
        } catch (Throwable t) { logErr("onBlockPlaceResult", t); }
    }

    /* ---- 注入 hook:entity_spawn(M8-D;ServerLevel.addFreshEntity(Entity)
       入口——服务端实体生成统一漏斗,含带乘客路径。
       Task 5 5.1/5.2:载荷 28B = mosaic_ev_entity { entity_id, entity_type,
       x, y, z, dimension, source }:
       - entity_type = BuiltInRegistries.ENTITY_TYPE.getId(entity.getType())
         (jb.h + hr.a(T);1.20.1 EntityType 无 getId());
       - dimension = FNV-1a-32(entity.level().dimension().location().toString())
         (bfj.dI() → cmm.ac() → acp.a() → toString,确定性可复核);
       - source = 0:1.20.1 addFreshEntity 入口钩子点不可得生成来源
         (addFreshEntity 无 cause 参数;来源追踪需网络/派生成因链路,超出
         本钩子能力——不实填充,注释如实) ---- */
    public static void onEntitySpawn(Object e) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            byte[] b = new byte[28];
            putIntLE(b, 0, (Integer) M_ID.invoke(e));
            int typeId = 0;
            try {
                Object type = M_ENT_TYPE.invoke(e);               /* getType() */
                if (type != null) {
                    Object reg = F_REG_ENTITY_TYPE.get(null);     /* BuiltInRegistries.ENTITY_TYPE */
                    if (reg != null) typeId = (Integer) M_REG_ENTITY_ID.invoke(reg, type);
                }
            } catch (Throwable t) { /* 注册 id 提取失败 → 0 */ }
            putIntLE(b, 4, typeId);
            putIntLE(b, 8, (int) Math.floor((Double) M_ENT_X.invoke(e)));
            putIntLE(b, 12, (int) Math.floor((Double) M_ENT_Y.invoke(e)));
            putIntLE(b, 16, (int) Math.floor((Double) M_ENT_Z.invoke(e)));
            int dim = 0;
            try {
                Object level = M_ENT_LEVEL.invoke(e);             /* level() */
                if (level != null) {
                    Object key = M_LEVEL_DIM.invoke(level);       /* dimension() → ResourceKey */
                    if (key != null) {
                        Object loc = M_RESKEY_LOC.invoke(key);    /* location() → ResourceLocation */
                        if (loc != null) dim = fnv1a32(loc.toString());
                    }
                }
            } catch (Throwable t) { /* dimension 提取失败 → 0 */ }
            putIntLE(b, 20, dim);
            putIntLE(b, 24, 0);   /* source:入口钩子不可得生成来源,固定 0 */
            dispatch(6, b);
        } catch (Throwable t) { logErr("onEntitySpawn", t); }
    }

    /* ---- 注入 hook:player_chat(M8-D;ServerGamePacketListenerImpl.handleChat
       入口——非 "/" 聊天包;player = aiy.b 字段。
       Task 5 5.5:入参追加 packet(handleChat 第 2 参 = ServerboundChatPacket),
       消息文本经 zi.a()(ServerboundChatPacket.message)提取,存入 chatMessage
       (静态 accessor chatMessage() 暴露给 Java 处理方) ---- */
    public static void onPlayerChat(Object handler, Object packet) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            if (packet != null && M_CHAT_MSG != null) {
                try { chatMessage = (String) M_CHAT_MSG.invoke(packet); }
                catch (Throwable t) { /* 提取失败保留上次值 */ }
            }
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

    /* ---- 注入 hook:网络域(Task 6 + Task 1 挂钩点迁移)
       入站大小:si.decode(ChannelHandlerContext,ByteBuf,List) 入口——分帧器
       (splitter)切好整包后每包恰好调用一次,buf.readableBytes() = 包字节数
       (不含分帧器 VarInt 长度前缀;压缩开启 = 压缩后字节数 + size 前缀)。
       大小与解码后类型跨挂钩传递:decode 与 channelRead0 同 channel 同 IO
       线程同步执行(decode → fireChannelRead → channelRead0 单调用栈,
       无穿插),经 ctx.channel().attr(AttributeKey "mosaic_packet_size")
       传递——多连接并发安全(attr 按 channel 隔离);channelRead0 派发后
       清除(set 0)。
       入站派发:sd.channelRead0(ChannelHandlerContext,Object) 桥接入口(包
       解码后;netty channelRead instanceof 校验后仅真实包到达)——保留在
       此(解码后类型经此取)。
       出站大小 + 派发:sj.a(ChannelHandlerContext,Packet,ByteBuf) =
       PacketEncoder.encode——1.20.1 混淆为 a(同类的 encode(Object,ByteBuf)
       为泛型擦除桥,javap 实测);入口把 out 缓冲存入 channel attr
       ("mosaic_enc_buf"),出口(唯一 RETURN,javap 实测;编码失败 ATHROW
       出口不派发)取 out.writerIndex() = 编码后字节数(不含 prepender
       追加的 VarInt 长度前缀)。旧挂钩 sd.doSendPacket 已移除——每包恰好
       派发一次(防双计,见 task-1-report.md)。
       出站 player 提取:encode 不在 Connection 方法内——经
       ctx.channel().pipeline().get(sd.class) 取 Connection 实例
       (configureSerialization addLast 把 sd 加入 pipeline,javap 反汇编
       证实,不依赖 handler 名)→ 与入站同一提取路径。
       载荷 12B = mosaic_ev_network { player_id, packet_id, size_hint }:
       player_id = sd.o(packetListener) instanceof aiy(ServerGamePacketListener
       Impl)→ aiy.b 字段(player);登录/状态/握手阶段 listener 非 aiy → 0。
       packet_id = PacketMap.OBFS 按 p.getClass().getName()(混淆全名)查表,
       未命中 → 0(UNKNOWN)。size_hint = 真实编码/解码字节长度(不可得 → 0)。
       防递归:钩子只读字段 + 查表 + 派发,不触发任何收发包路径(派发本身
       不经网络;包路径触发的事件在钩子外)。 ---- */

    /* si.decode 入口:记录帧字节数到 channel attr(packet_size) */
    public static void onPacketDecodeStart(Object ctx, Object buf) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            Object attr = channelAttr(ctx, ATTR_SIZE_KEY);
            if (attr != null) M_ATTR_SET.invoke(attr, (Integer) M_BUF_READABLE.invoke(buf));
        } catch (Throwable t) { logErr("onPacketDecodeStart", t); }
    }

    /* sd.channelRead0 入口:取 decode 记录的包大小(派发后清除)→ 派发 */
    public static void onPacketReceived(Object conn, Object ctx, Object packet) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            int size = 0;
            try {
                Object attr = channelAttr(ctx, ATTR_SIZE_KEY);
                if (attr != null) {
                    Object v = M_ATTR_GET.invoke(attr);
                    if (v != null) size = (Integer) v;
                    M_ATTR_SET.invoke(attr, 0);   /* 派发后清除(防陈旧值) */
                }
            } catch (Throwable t) { /* attr 读取失败 → size=0 */ }
            dispatchPacket(conn, packet, EV_PACKET_RECV, size);
        } catch (Throwable t) { logErr("onPacketReceived", t); }
    }

    /* sj.a(encode)入口:out 缓冲存入 channel attr(enc_buf),出口取长度 */
    public static void onPacketEncodeStart(Object ctx, Object packet, Object out) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            Object attr = channelAttr(ctx, ATTR_ENC_KEY);
            if (attr != null) M_ATTR_SET.invoke(attr, out);
        } catch (Throwable t) { logErr("onPacketEncodeStart", t); }
    }

    /* sj.a(encode)出口(每个 RETURN):out.writerIndex() = 编码后字节数;
       player 经 pipeline 取 Connection(sd) 实例提取(见上) */
    public static void onPacketSent(Object ctx, Object packet) {
        try {
            if (rt == 0) return;
            resolve();
            if (!reflected) return;
            int size = 0;
            try {
                Object attr = channelAttr(ctx, ATTR_ENC_KEY);
                if (attr != null) {
                    Object out = M_ATTR_GET.invoke(attr);
                    M_ATTR_SET.invoke(attr, (Object) null);   /* 清除 */
                    if (out != null) size = (Integer) M_BUF_WRITER.invoke(out);
                }
            } catch (Throwable t) { /* attr/长度提取失败 → size=0 */ }
            Object conn = null;
            try {
                Object pipe = M_CHANNEL_PIPE.invoke(M_CTX_CHANNEL.invoke(ctx));
                if (pipe != null) conn = M_PIPE_GET.invoke(pipe, C_CONN);
            } catch (Throwable t) { /* pipeline 提取失败 → conn=null(非游戏阶段) */ }
            dispatchPacket(conn, packet, EV_PACKET_SENT, size);
        } catch (Throwable t) { logErr("onPacketSent", t); }
    }

    /* ctx → channel 的 attr(指定 AttributeKey) */
    private static Object channelAttr(Object ctx, Object key) throws Exception {
        Object ch = M_CTX_CHANNEL.invoke(ctx);
        if (ch == null) return null;
        return M_ATTR.invoke(ch, key);
    }

    private static void dispatchPacket(Object conn, Object packet, int idx, int size) {
        if (rt == 0) return;
        resolve();
        if (!reflected) return;
        int playerId = 0;
        if (conn != null) {
            try {
                Object listener = F_PACKET_LISTENER.get(conn);
                if (listener != null && C_AIY.isInstance(listener)) {
                    Object player = F_AIY_PLAYER.get(listener);   /* aiy.b */
                    if (player != null) playerId = (Integer) M_ID.invoke(player);
                }
            } catch (Throwable t) { /* listener/player 提取失败 → player_id=0 */ }
        }
        int packetId = 0;
        try {
            Integer id = PacketMap.OBFS.get(packet.getClass().getName());
            if (id != null) packetId = id;
        } catch (Throwable t) { /* 查表失败 → 0(UNKNOWN) */ }
        byte[] b = new byte[12];
        putIntLE(b, 0, playerId);
        putIntLE(b, 4, packetId);
        putIntLE(b, 8, size);   /* size_hint:真实编码/解码字节长度(不可得 → 0) */
        dispatch(idx, b);
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
               tick 的派发即覆盖其订阅者(dispatch 遍历 rt->packs)。
               [M9 修复] 路径含空格:split("\\s+") 会截断 parts[2],改为取
               "install " 前缀之后的整段命令串(indexOf("install") + 8 =
               "install " 长度 8)。 */
            if (parts.length < 3) { usage(); return; }
            String p = cmd.substring(cmd.indexOf("install") + 8).trim();
            if (p.isEmpty()) { usage(); return; }
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
            warnIfTimeout(idx);
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
            case "player_command": {                    /* player_id/cmd_hash = 2×u32 */
                byte[] bc = new byte[8];
                for (int i = 0; i < 2; i++)
                    putIntLE(bc, i * 4, vals.length > i ? vals[i] : 0);
                return bc;
            }
            case "packet_received": case "packet_sent": /* player_id/packet_id/size_hint = 3×u32 */
            {   byte[] bn = new byte[12];
                for (int i = 0; i < 3; i++)
                    putIntLE(bn, i * 4, vals.length > i ? vals[i] : 0);
                return bn;
            }
            case "block_break": case "block_place":     /* player_id/x/y/z/block_type = 5×u32 */
            case "entity_spawn": {                      /* entity_id/entity_type/x/y/z/dimension/source = 7×u32 */
                int n = EVENTS[idx].equals("entity_spawn") ? 7 : 5;
                byte[] b = new byte[n * 4];
                for (int i = 0; i < n; i++)
                    putIntLE(b, i * 4, vals.length > i ? vals[i] : 0);
                return b;
            }
            default:
                return null;
        }
    }

    /* FNV-1a-32(dimension location 串与命令文本共用;与报告复算结果一致,
       确定性可复核) */
    private static int fnv1a32(String s) {
        int h = 0x811c9dc5;
        for (int i = 0; i < s.length(); i++) {
            h ^= s.charAt(i);
            h *= 0x01000193;
        }
        return h;
    }

    /* 派发:事件未注册(-1)→ 跳过;返回执行数累积到静态计数器。
       M9:派发后检查超时(预算生效时 lastError 反映本次派发结果;
       200ms 内正常完成 = 0,慢订阅者被跳过 = MOSAIC_ERR_TIMEOUT → 告警)。 */
    private static void dispatch(int idx, byte[] payload) {
        if (EV_IDS[idx] < 0) return;
        int n = Bridge.eventDispatch(rt, EV_IDS[idx], payload);
        if (n > 0) EV_EXEC[idx] += n;
        EV_CALLS[idx]++;
        warnIfTimeout(idx);
    }

    /* 超时告警(仅日志):节流 5s 是全局的(单一时间戳,任一事件超时告警后
       5s 内不再重复——并非每事件各自节流);慢订阅者常驻时不刷屏。语义:该
       事件预算内未完成,剩余订阅者被跳过(正在执行的函数不受影响,不能被
       中断)。 */
    private static long lastTimeoutWarn = 0;
    private static void warnIfTimeout(int idx) {
        if (Bridge.lastError(rt) != MOSAIC_ERR_TIMEOUT) return;
        long now = System.currentTimeMillis();
        if (now - lastTimeoutWarn < 5000) return;
        lastTimeoutWarn = now;
        System.out.println("Mosaic agent: WARN event \"" + EVENTS[idx]
                + "\" dispatch exceeded budget: slow subscriber(s) skipped"
                + " (err=" + MOSAIC_ERR_TIMEOUT + ")");
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
