package com.mosaic.agent;

import java.lang.instrument.ClassFileTransformer;
import java.security.ProtectionDomain;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
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
 * 全部经 Mojang server_mappings + javap 核实):
 *   - alk = net.minecraft.server.players.PlayerList
 *       a(Lsd;Laig;)V = placeNewPlayer    → 方法尾注入 onPlayerJoin(ServerPlayer)
 *       c(Laig;)V     = remove            → 方法尾注入 onPlayerLeave(ServerPlayer)
 *   - aih = net.minecraft.server.level.ServerPlayerGameMode
 *       a(Lgu;)Z      = destroyBlock      → 方法入口注入 onBlockBreak(gm,pos)
 *                                          (入口注入 = 方块尚未破坏,取破坏前状态)
 *   - dt = net.minecraft.commands.Commands
 *       a(Lds;Ljava/lang/String;)I = performPrefixedCommand(控制台/聊天命令漏斗)
 *                                          → 方法入口注入 onCommand;返回 true 则
 *                                            ICONST_1 IRETURN 消费命令
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

    /* 注入规格:internal:name:desc → (kind, locals, hook, hookDesc) */
    private static final Map<String, Spec> SPECS = new HashMap<>();
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
        DISPLAY.put("net/minecraft/server/MinecraftServer", "net.minecraft.server.MinecraftServer");
    }

    private enum Kind { END, START, CONSUME }

    private static final class Spec {
        Kind kind; int[] locals; String hook, hookDesc;
        Spec(Kind kind, int[] locals, String hook, String hookDesc) {
            this.kind = kind; this.locals = locals;
            this.hook = hook; this.hookDesc = hookDesc;
        }
    }

    private static void put(String cls, String name, String desc, Kind kind,
                            int[] locals, String hook, String hookDesc) {
        SPECS.put(cls + ":" + name + ":" + desc, new Spec(kind, locals, hook, hookDesc));
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
            ClassWriter cw = new ClassWriter(
                    ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS) {
                @Override
                protected String getCommonSuperClass(String t1, String t2) {
                    if (t1.equals(t2)) return t1;
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
            ClassVisitor cv = new ClassVisitor(Opcodes.ASM9, cw) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                    Spec spec = SPECS.get(className + ":" + name + ":" + desc);
                    if (spec == null || mv == null) return mv;
                    changed[0] = true;
                    return new InjectingMethodVisitor(mv, spec);
                }
            };
            new ClassReader(classfileBuffer).accept(cv, 0);
            if (!changed[0]) return null;
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

        InjectingMethodVisitor(MethodVisitor mv, Spec spec) {
            super(Opcodes.ASM9, mv);
            this.spec = spec;
        }

        @Override
        public void visitCode() {
            super.visitCode();
            if (spec.kind == Kind.START) {
                for (int l : spec.locals) super.visitVarInsn(Opcodes.ALOAD, l);
                super.visitMethodInsn(Opcodes.INVOKESTATIC, HOOKS, spec.hook,
                        spec.hookDesc, false);
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
            }
            super.visitInsn(opcode);
        }
    }
}
