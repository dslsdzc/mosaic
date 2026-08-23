package com.mosaic.agent;

import java.lang.instrument.ClassFileTransformer;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * M4-2:自研 ClassFileTransformer(注入引擎自研;ASM 仅作字节码编辑工具库,
 * 只用其单一核心 jar 的 core API——ClassReader/ClassVisitor/ClassWriter)。
 *
 * 白名单(1.20.1 官方 server.jar 混淆名,mojmap → 混淆映射见 MosaicHooks 注释;
 * 全部经 Mojang server_mappings + javap 核实;M8-D 新增 5 点,签名核实
 * 记录见 .superpowers/sdd/task-m8-d-report.md):
 *   - alk = net.minecraft.server.players.PlayerList
 *       a(Lsd;Laig;)V = placeNewPlayer    → 方法尾注入 onPlayerJoin(ServerPlayer)
 *       c(Laig;)V     = remove            → 方法尾注入 onPlayerLeave(ServerPlayer)
 *   - aih = net.minecraft.server.level.ServerPlayerGameMode
 *       a(Lgu;)Z      = destroyBlock      → 方法入口注入 onBlockBreak(gm,pos)
 *                                          (入口注入 = 方块尚未破坏,取破坏前状态)
 *   - dt = net.minecraft.commands.Commands
 *       a(Lds;Ljava/lang/String;)I = performPrefixedCommand(控制台/RCON 命令漏斗)
 *                                          → 方法入口注入 onCommand;返回 true 则
 *                                            ICONST_1 IRETURN 消费命令
 *       a(Lcom/mojang/brigadier/ParseResults;Ljava/lang/String;)I =
 *          performCommand(ParseResults,String) 游戏内聊天命令漏斗
 *          (ServerGamePacketListenerImpl.performChatCommand 反汇编证实:
 *          CommandDispatcher.parse → dt.a(ParseResults,UnaryOperator) →
 *          dt.a(ParseResults,String) 执行;返回结果被调用方 pop 丢弃)
 *          → 方法入口注入 onChatCommand;返回 true 则 ICONST_1 IRETURN 消费
 *   - cds = net.minecraft.world.item.BlockItem
 *       a(Lcih;Ldcb;)Z = placeBlock(BlockPlaceContext,BlockState)
 *          (1.20.1 放置成功路径的稳定注入点:place() 校验通过后才调
 *          placeBlock,入参 state 即实际放置的方块状态)
 *          → Task 5 5.4 改为返回值出口钩子 onBlockPlaceResult(ok,ctx,state):
 *            入口把 ctx/state 存入新局部槽(3/4);方法所有 IRETURN 前
 *            dup 返回值 + ALOAD 两局部槽 → invokestatic —— 钩子收到
 *            placeBlock 真实返回值,返回 false(放置失败)不派发
 *   - aif = net.minecraft.server.level.ServerLevel
 *       b(Lbfj;)Z     = addFreshEntity   → 方法入口注入 onEntitySpawn(entity)
 *                                          (服务端实体生成统一漏斗:
 *                                          addFreshEntityWithPassengers → addFreshEntity
 *                                          → addEntity,javap 证实)
 *   - aiy = net.minecraft.server.network.ServerGamePacketListenerImpl
 *       a(Lzi;)V      = handleChat(ServerboundChatPacket)(非 "/" 聊天包入口;
 *                                          "/" 命令走 zh 包路径,不入此 hook)
 *          → 方法入口注入 onPlayerChat(handler,packet)(player 字段 = aiy.b;
 *            Task 5 5.5:packet = 第 2 参,消息文本提取)
 *   - aig = net.minecraft.server.level.ServerPlayer
 *       a(Lben;)V     = die(DamageSource)→ 方法入口注入 onPlayerDeath(player)
 *   - sd = net.minecraft.network.Connection(Task 6 网络域;核实见
 *       .superpowers/sdd/task-6-report.md)
 *       channelRead0(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V
 *          = SimpleChannelInboundHandler 桥接入口(包解码后;netty channelRead
 *          instanceof 校验后仅真实包调用;typed 版本 a(Ctx;Luo;)V 为 1.20.1
 *          server.txt 的 channelRead0(Ctx,Packet) 混淆名,javap 双证)
 *          → 方法入口注入 onPacketReceived(conn, ctx, packet)
 *          (Task 1:入站派发保留在此——解码后类型经此取;size 由 si.decode
 *          经 Channel.attr 传至此处,派发后清除,防双计)
 *       [Task 1 移除] a(Luo;Lsl;Lse;Lse;)V = doSendPacket 出站派发挂钩——
 *          迁至 sj(PacketEncoder)encode 出口(防双计:每包恰好派发一次)
 *   - si = net.minecraft.network.PacketDecoder(Task 1;核实见
 *       .superpowers/sdd/task-1-report.md)
 *       decode(Lio/netty/channel/ChannelHandlerContext;Lio/netty/buffer/ByteBuf;
 *          Ljava/util/List;)V = 分帧后每包恰好一次的解码入口(splitter 帧已
 *          切好整包;buf.readableBytes() = 包字节数)
 *          → 方法入口注入 onPacketDecodeStart(ctx, buf):大小经
 *            ctx.channel().attr 传递,channelRead0 派发后清除
 *   - sj = net.minecraft.network.PacketEncoder(Task 1;核实见
 *       .superpowers/sdd/task-1-report.md)
 *       a(Lio/netty/channel/ChannelHandlerContext;Luo;Lio/netty/buffer/ByteBuf;)V
 *          = encode(ChannelHandlerContext,Packet,ByteBuf)(1.20.1 混淆为 a;
 *          同类的 encode(Ctx;Object;ByteBuf) 为泛型擦除桥,MessageToByteEncoder
 *          write → 桥 → a,每包恰好一次;javap 实测 a 只有一个 RETURN(正常
 *          路径),其余出口为 ATHROW(编码失败不派发 packet_sent))
 *          → 入口注入 onPacketEncodeStart(ctx, packet, out):out 存入
 *            ctx.channel().attr;每个 RETURN 前注入 onPacketSent(ctx, packet):
 *            attr 取 out → writerIndex() = 编码后字节数 → 派发(出站 player
 *            经 ctx.channel().pipeline().get(sd) 取 Connection 实例)
 *   - net/minecraft/server/MinecraftServer
 *       a(Ljava/util/function/BooleanSupplier;)V = tickServer → 方法尾注入
 *                                          onServerTick(this)(每 tick 派发 "tick")
 *
 * hook 参数以 Ljava/lang/Object; 承载(1.20.1 混淆名无法在源码编译期引用;
 * MosaicHooks 内按混淆名反射访问),invokestatic 描述符与 MosaicHooks 源码一致。
 *
 * 转换失败/未知类 → 返回 null(原样加载);任何异常吞掉并返回 null——注入
 * 代码不得崩服务端,最坏情况 = 该类保持 vanilla。
 */
public final class MosaicTransformer implements ClassFileTransformer {

    private static final String HOOKS = "com/mosaic/agent/MosaicHooks";

    /* 注入规格:internal:name:desc → (kind, locals, hook, hookDesc) 列表——
       同一方法可挂多个 hook(sj.a 入口 + 出口,Task 1);访问器按序链式
       包装,注入顺序 = put 顺序 */
    private static final Map<String, List<Spec>> SPECS = new HashMap<>();
    static {
        /* alk = net.minecraft.server.players.PlayerList */
        put("alk", "a", "(Lsd;Laig;)V", Kind.END, new int[]{2},
            "onPlayerJoin", "(Ljava/lang/Object;)V");
        put("alk", "c", "(Laig;)V", Kind.END, new int[]{1},
            "onPlayerLeave", "(Ljava/lang/Object;)V");
        /* aih = net.minecraft.server.level.ServerPlayerGameMode */
        put("aih", "a", "(Lgu;)Z", Kind.START, new int[]{0, 1},
            "onBlockBreak", "(Ljava/lang/Object;Ljava/lang/Object;)V");
        /* dt = net.minecraft.commands.Commands(performPrefixedCommand) */
        put("dt", "a", "(Lds;Ljava/lang/String;)I", Kind.CONSUME, null,
            "onCommand", "(Ljava/lang/Object;Ljava/lang/String;)Z");
        /* dt = net.minecraft.commands.Commands(performCommand(ParseResults,String),
           游戏内聊天命令漏斗,M8-D) */
        put("dt", "a", "(Lcom/mojang/brigadier/ParseResults;Ljava/lang/String;)I",
            Kind.CONSUME, null,
            "onChatCommand", "(Ljava/lang/Object;Ljava/lang/String;)Z");
        /* cds = net.minecraft.world.item.BlockItem(placeBlock(BlockPlaceContext,
           BlockState) = 1.20.1 放置成功路径的稳定注入点,M8-D;Task 5 5.4 改
           返回值出口钩子:locals = {ctx 入参槽, state 入参槽, 新局部槽 ctx,
           新局部槽 state}——(Lcih;Ldcb;)Z 无 long/double 入参,新槽 3/4
           空闲;返回 false(放置失败)不派发) */
        put("cds", "a", "(Lcih;Ldcb;)Z", Kind.RETVAL, new int[]{1, 2, 3, 4},
            "onBlockPlaceResult", "(ZLjava/lang/Object;Ljava/lang/Object;)V");
        /* aif = net.minecraft.server.level.ServerLevel(addFreshEntity,M8-D) */
        put("aif", "b", "(Lbfj;)Z", Kind.START, new int[]{1},
            "onEntitySpawn", "(Ljava/lang/Object;)V");
        /* aiy = ServerGamePacketListenerImpl(handleChat,M8-D;Task 5 5.5 追加
           packet 入参(第 2 参 local 1)供消息文本提取) */
        put("aiy", "a", "(Lzi;)V", Kind.START, new int[]{0, 1},
            "onPlayerChat", "(Ljava/lang/Object;Ljava/lang/Object;)V");
        /* aig = net.minecraft.server.level.ServerPlayer(die(DamageSource),M8-D) */
        put("aig", "a", "(Lben;)V", Kind.START, new int[]{0},
            "onPlayerDeath", "(Ljava/lang/Object;)V");
        /* sd = net.minecraft.network.Connection(Task 6 网络域)
           入站:channelRead0(ChannelHandlerContext,Object) 桥接入口(包解码后;
           local 0=this, 1=ctx, 2=packet → 取 {0,1,2};Task 1:入参追加 ctx,
           从 channel attr 读 si.decode 入口记录的大小,派发后清除;
           出站 doSendPacket 挂钩已移除(Task 1 迁至 sj encode 出口) */
        put("sd", "channelRead0",
            "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V",
            Kind.START, new int[]{0, 1, 2},
            "onPacketReceived", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
        /* si = net.minecraft.network.PacketDecoder(Task 1;核实见 task-1-report)
           decode(ChannelHandlerContext, ByteBuf, List) 入口——splitter 帧
           切好后每包恰好一次,local 1=ctx, 2=buf(帧整包)→ {1,2};
           大小 = buf.readableBytes() → channel attr(packet_size) */
        put("si", "decode",
            "(Lio/netty/channel/ChannelHandlerContext;Lio/netty/buffer/ByteBuf;Ljava/util/List;)V",
            Kind.START, new int[]{1, 2},
            "onPacketDecodeStart", "(Ljava/lang/Object;Ljava/lang/Object;)V");
        /* sj = net.minecraft.network.PacketEncoder(Task 1;核实见 task-1-report)
           a(Ctx;Luo;ByteBuf) = encode(ChannelHandlerContext,Packet,ByteBuf),
           local 1=ctx, 2=packet, 3=out;入口:out 存入 channel attr(enc_buf);
           出口(唯一 RETURN,javap 实测):attr 取 out → writerIndex() =
           编码后字节数 → 派发 packet_sent(编码失败 ATHROW 出口不派发) */
        put("sj", "a",
            "(Lio/netty/channel/ChannelHandlerContext;Luo;Lio/netty/buffer/ByteBuf;)V",
            Kind.START, new int[]{1, 2, 3},
            "onPacketEncodeStart", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)V");
        put("sj", "a",
            "(Lio/netty/channel/ChannelHandlerContext;Luo;Lio/netty/buffer/ByteBuf;)V",
            Kind.END, new int[]{1, 2},
            "onPacketSent", "(Ljava/lang/Object;Ljava/lang/Object;)V");
        /* MinecraftServer.tickServer(未混淆) */
        put("net/minecraft/server/MinecraftServer", "a",
            "(Ljava/util/function/BooleanSupplier;)V", Kind.END, new int[]{0},
            "onServerTick", "(Ljava/lang/Object;)V");
    }
    private static final Map<String, String> DISPLAY = new HashMap<>();
    static {
        DISPLAY.put("alk", "net.minecraft.server.players.PlayerList");
        DISPLAY.put("aih", "net.minecraft.server.level.ServerPlayerGameMode");
        DISPLAY.put("dt", "net.minecraft.commands.Commands");
        DISPLAY.put("cds", "net.minecraft.world.item.BlockItem");
        DISPLAY.put("aif", "net.minecraft.server.level.ServerLevel");
        DISPLAY.put("aiy", "net.minecraft.server.network.ServerGamePacketListenerImpl");
        DISPLAY.put("aig", "net.minecraft.server.level.ServerPlayer");
        DISPLAY.put("sd", "net.minecraft.network.Connection");
        DISPLAY.put("si", "net.minecraft.network.PacketDecoder");
        DISPLAY.put("sj", "net.minecraft.network.PacketEncoder");
        DISPLAY.put("net/minecraft/server/MinecraftServer", "net.minecraft.server.MinecraftServer");
    }

    /* RETVAL(Task 5 5.4):入口把 locals[0..1] 入参存入新局部槽 locals[2..3];
       每个 IRETURN 前 dup 返回值 + ALOAD 两局部槽 → invokestatic 钩子
       (钩子收到真实返回值;局部槽按帧保存,嵌套调用天然安全) */
    private enum Kind { END, START, CONSUME, RETVAL }

    private static final class Spec {
        Kind kind; int[] locals; String hook, hookDesc;
        Spec(Kind kind, int[] locals, String hook, String hookDesc) {
            this.kind = kind; this.locals = locals;
            this.hook = hook; this.hookDesc = hookDesc;
        }
    }

    private static void put(String cls, String name, String desc, Kind kind,
                            int[] locals, String hook, String hookDesc) {
        String key = cls + ":" + name + ":" + desc;
        List<Spec> list = SPECS.get(key);
        if (list == null) { list = new ArrayList<Spec>(); SPECS.put(key, list); }
        list.add(new Spec(kind, locals, hook, hookDesc));
    }

    /* 服务端类的定义加载器(1.20.1 bundler 自建 URLClassLoader 加载 unpacked
       服务端,非系统加载器)——帧计算的公共父类解析与 MosaicHooks 反射都必须
       用它;系统加载器 Class.forName 看不到服务端类。 */
    private static volatile ClassLoader serverLoader;

    static ClassLoader serverLoader() { return serverLoader; }

    private final Set<String> transformed =
            Collections.synchronizedSet(new HashSet<String>());

    @Override
    public byte[] transform(ClassLoader loader, String className,
                            Class<?> classBeingRedefined, ProtectionDomain protectionDomain,
                            byte[] classfileBuffer) {
        if (className == null) return null;
        String display = DISPLAY.get(className);
        if (display == null || classBeingRedefined != null) return null;
        serverLoader = loader;

        try {
            final boolean[] changed = {false};
            /* 帧:COMPUTE_FRAMES 重算(类加载期,目标类可能尚未加载;
               公共父类用定义加载器解析,失败回退 Object——最坏情况帧精度下降) */
            /* 正在转换的类:定义尚未完成,Class.forName 会重入 loadClass →
               findClass → 同一名字二次 defineClass → "attempted duplicate
               class definition" LinkageError(实测:转换 aig(ServerPlayer)时
               其方法帧合并需要 commonSuper(aig, bfj),Class.forName("aig")
               重入即崩;M8-D 新增 aig 转换后触发)。处理:把 current 替换为
               其直接父类(父类在子类 defineClass 前必已加载,可安全加载)。
               [M8-D 评审修正 2026-08-23] 原注释"vanilla 无类继承
               aig/aiy/cds/aif"不实——javap 全量扫描 server-1.20.1.jar
               (5440 类)复核:aig(ServerPlayer)有子类 pq$3,cds(BlockItem)
               有 10 个子类(cdr/cem/cfi/cfl/cfy/cgm/cgt/cgz/chd/chi,8 直接);
               aif/aiy 无子类(评审所举 dgu$d 实为 interface,"6 类继承 aif"
               未复现)。但子类类型不参与被转换类自身帧合并(帧合并仅涉及
               current 方法栈上的类型,current 自身按直接父类参与),故
               LUB(current,t2)=LUB(super(current),t2) 对该场景依然成立。
               注意不能简单回退 Object——帧合并类型被放宽后,下游
               putfield/invokevirtual 仍按 bfj 校验会 VerifyError
               (实测 aig.c(Lbfj;)V)。 */
            final String[] currentSuper = {null};
            try {
                new ClassReader(classfileBuffer).accept(new ClassVisitor(Opcodes.ASM9) {
                    @Override
                    public void visit(int version, int access, String name, String signature,
                                      String superName, String[] interfaces) {
                        currentSuper[0] = superName;
                    }
                }, 0);
            } catch (Throwable t) { /* superName 解析失败 → 保持 null,走下方回退 */ }
            final String current = className;
            ClassWriter cw = new ClassWriter(
                    ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS) {
                @Override
                protected String getCommonSuperClass(String t1, String t2) {
                    if (t1.equals(t2)) return t1;
                    if (t1.equals(current)) {
                        if (currentSuper[0] == null) return "java/lang/Object";
                        t1 = currentSuper[0];
                    } else if (t2.equals(current)) {
                        if (currentSuper[0] == null) return "java/lang/Object";
                        t2 = currentSuper[0];
                    }
                    if (t1.equals(t2)) return t1;   /* 替换后可能收敛 */
                    ClassLoader cl = serverLoader != null ? serverLoader
                            : MosaicTransformer.class.getClassLoader();
                    try {
                        Class<?> c1 = Class.forName(t1.replace('/', '.'), false, cl);
                        Class<?> c2 = Class.forName(t2.replace('/', '.'), false, cl);
                        return commonSuper(c1, c2).getName().replace('.', '/');
                    } catch (Throwable t) {
                        return "java/lang/Object";
                    }
                }
            };
            final int[] retvalMaxLocals = { -1 };   /* F-16:RETVAL 原方法 max_locals 捕获 */
            ClassVisitor cv = new ClassVisitor(Opcodes.ASM9, cw) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                    List<Spec> specs = SPECS.get(className + ":" + name + ":" + desc);
                    if (specs == null || mv == null) return mv;
                    changed[0] = true;
                    /* 多 hook 链式包装:外层访问器先注入(入口 hook 先于出口 hook
                       执行;互不干扰,同一方法不同 hook 各注入一次) */
                    for (Spec spec : specs)
                        mv = new InjectingMethodVisitor(mv, spec, retvalMaxLocals);
                    return mv;
                }
            };
            new ClassReader(classfileBuffer).accept(cv, 0);
            if (!changed[0]) return null;
            /* F-16:RETVAL 注入依赖新局部槽 3/4 空闲(入口把入参 ctx/state 存入
               槽 3/4)——原方法 max_locals 必须 == 3(当前 cds.a 实测 3)。若未来
               重混淆使该方法占用槽 3/4,继续注入会静默覆盖其局部变量(错值不可
               察觉):此处校验失败 → 返回 null 原样加载(注入跳过),InjectCheck
               的 ALL INJECTED 门禁随之 MISS 大声失败,而非静默错值。 */
            if (retvalMaxLocals[0] != -1 && retvalMaxLocals[0] != 3) {
                System.out.println("Mosaic agent: WARN " + className
                        + " RETVAL 原方法 max_locals=" + retvalMaxLocals[0]
                        + " != 3(注入需局部槽 3/4 空闲)——类原样加载,注入跳过"
                        + "(InjectCheck 将 MISS,防静默错值)");
                return null;
            }
            if (transformed.add(className)) {
                System.out.println("Mosaic agent: transformed " + className
                        + " (" + display + ")");
            }
            return cw.toByteArray();
        } catch (Throwable t) {
            System.out.println("Mosaic agent: transform failed for " + className
                    + ": " + t + " (class left untransformed)");
            return null;
        }
    }

    private static Class<?> commonSuper(Class<?> a, Class<?> b) {
        if (a.isAssignableFrom(b)) return a;
        if (b.isAssignableFrom(a)) return b;
        if (a.isInterface() || b.isInterface()) return Object.class;
        do {
            a = a.getSuperclass();
        } while (a != null && !a.isAssignableFrom(b));
        return a == null ? Object.class : a;
    }

    /* 注入用 MethodVisitor:按 spec 在方法入口/尾插 invokestatic */
    private static final class InjectingMethodVisitor extends MethodVisitor {

        private final Spec spec;
        /* F-16:RETVAL 原方法 max_locals 捕获(visitMaxs 收到的是类文件原始值,
           COMPUTE_MAXS 只影响 writer 侧计算,不改写本访问器收到的入参) */
        private final int[] retvalMaxLocals;

        InjectingMethodVisitor(MethodVisitor mv, Spec spec, int[] retvalMaxLocals) {
            super(Opcodes.ASM9, mv);
            this.spec = spec;
            this.retvalMaxLocals = retvalMaxLocals;
        }

        @Override
        public void visitMaxs(int maxStack, int maxLocals) {
            if (spec.kind == Kind.RETVAL) retvalMaxLocals[0] = maxLocals;
            super.visitMaxs(maxStack, maxLocals);
        }

        @Override
        public void visitCode() {
            super.visitCode();
            if (spec.kind == Kind.START) {
                for (int l : spec.locals) super.visitVarInsn(Opcodes.ALOAD, l);
                super.visitMethodInsn(Opcodes.INVOKESTATIC, HOOKS, spec.hook,
                        spec.hookDesc, false);
            } else if (spec.kind == Kind.RETVAL) {
                /* 入口:args locals[0..1] → 新局部槽 locals[2..3]
                   (COMPUTE_MAXS 自动扩展 max_locals) */
                super.visitVarInsn(Opcodes.ALOAD, spec.locals[0]);
                super.visitVarInsn(Opcodes.ASTORE, spec.locals[2]);
                super.visitVarInsn(Opcodes.ALOAD, spec.locals[1]);
                super.visitVarInsn(Opcodes.ASTORE, spec.locals[3]);
            } else if (spec.kind == Kind.CONSUME) {
                super.visitVarInsn(Opcodes.ALOAD, 1);   /* CommandSourceStack */
                super.visitVarInsn(Opcodes.ALOAD, 2);   /* String command */
                super.visitMethodInsn(Opcodes.INVOKESTATIC, HOOKS, spec.hook,
                        spec.hookDesc, false);
                Label cont = new Label();
                super.visitJumpInsn(Opcodes.IFEQ, cont);
                super.visitInsn(Opcodes.ICONST_1);
                super.visitInsn(Opcodes.IRETURN);
                super.visitLabel(cont);
            }
        }

        @Override
        public void visitInsn(int opcode) {
            if (spec.kind == Kind.END && opcode >= Opcodes.IRETURN
                    && opcode <= Opcodes.RETURN) {
                /* 每个返回路径前插 loads + invokestatic(返回路径互斥,
                   每次方法调用恰命中一条 → 每调用恰好派发一次) */
                for (int l : spec.locals) super.visitVarInsn(Opcodes.ALOAD, l);
                super.visitMethodInsn(Opcodes.INVOKESTATIC, HOOKS, spec.hook,
                        spec.hookDesc, false);
            } else if (spec.kind == Kind.RETVAL && opcode == Opcodes.IRETURN) {
                /* 返回值出口:栈顶 = boolean 返回值。
                   DUP → [v, v];ALOAD ctx/state → [v, v, ctx, state];
                   invokestatic (Z;Object;Object;)V 消费 3 → 留 [v] → IRETURN */
                super.visitInsn(Opcodes.DUP);
                super.visitVarInsn(Opcodes.ALOAD, spec.locals[2]);
                super.visitVarInsn(Opcodes.ALOAD, spec.locals[3]);
                super.visitMethodInsn(Opcodes.INVOKESTATIC, HOOKS, spec.hook,
                        spec.hookDesc, false);
            }
            super.visitInsn(opcode);
        }
    }
}
