import java.io.*;
import java.util.*;
import java.util.jar.*;
import java.lang.instrument.ClassFileTransformer;
import org.objectweb.asm.*;

/** 字节码注入校验(M8-D 评审 Issue-5 固化):对 1.20.1 server.jar 的真实类字节
 *  跑真实 MosaicTransformer,用 ASM 扫描输出字节码中
 *  invokestatic com/mosaic/agent/MosaicHooks.<hook> 调用——SPEC 命中则注入,
 *  输出字节用 ClassReader 重读(帧冒烟)。最小化校验:防注入点签名漂移
 *  (混淆名/desc 变了但 SPECS 没跟上时,此处立即 MISS)。
 *
 * 用法:java -cp lib/asm.jar:build/agentclasses:build/injectcheck \
 *         InjectCheck mc-server/minecraft_server.1.20.1.jar
 * 成功输出末行 "ALL INJECTED",否则 "INJECTION GAP"(build_mc_agent.sh 门禁)。
 */
public class InjectCheck {
    public static void main(String[] args) throws Exception {
        String jarPath = args[0];
        /* 期望注入点 = MosaicTransformer.SPECS 全量(11 类 14 方法;M9 补齐
           aih→onBlockBreak 与 MinecraftServer→onServerTick 两个既有点;
           Task 5 5.4:cds 改返回值出口钩子 onBlockPlaceResult;
           Task 6:sd(Connection)→ onPacketReceived 入站挂钩;
           Task 1:挂钩点迁移——出站从 sd.doSendPacket 迁到
           sj(PacketEncoder).a encode 入口+出口(onPacketEncodeStart +
           onPacketSent),入站大小取 si(PacketDecoder).decode 入口
           (onPacketDecodeStart),channelRead0 保留类型提取(签名加 ctx)。
           类名 = jar 内条目名:混淆类直接是混淆名,MinecraftServer 未混淆。 */
        String[][] expect = {
            {"dt",  "onCommand", "onChatCommand"},
            {"aif", "onEntitySpawn"},
            {"aig", "onPlayerDeath"},
            {"aiy", "onPlayerChat"},
            {"cds", "onBlockPlaceResult"},
            {"alk", "onPlayerJoin", "onPlayerLeave"},
            {"aih", "onBlockBreak"},
            {"sd",  "onPacketReceived"},
            {"si",  "onPacketDecodeStart"},
            {"sj",  "onPacketEncodeStart", "onPacketSent"},
            {"net/minecraft/server/MinecraftServer", "onServerTick"},
        };
        JarFile jar = new JarFile(jarPath);
        boolean allOk = true;
        for (String[] e : expect) {
            String cls = e[0];
            JarEntry je = jar.getJarEntry(cls + ".class");
            if (je == null) {
                System.out.println("FAIL: " + cls + ".class not in " + jarPath
                        + " (jar format changed?)");
                allOk = false;
                continue;
            }
            byte[] orig = jar.getInputStream(je).readAllBytes();
            byte[] out = new com.mosaic.agent.MosaicTransformer().transform(
                    InjectCheck.class.getClassLoader(), cls, null, null, orig);
            if (out == null) { System.out.println("FAIL: " + cls + " not transformed (SPEC miss?)"); allOk = false; continue; }
            final Set<String> found = new HashSet<>();
            new ClassReader(out).accept(new ClassVisitor(Opcodes.ASM9) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    return new MethodVisitor(Opcodes.ASM9) {
                        @Override
                        public void visitMethodInsn(int op, String owner, String mname,
                                                    String mdesc, boolean itf) {
                            if (op == Opcodes.INVOKESTATIC
                                    && owner.equals("com/mosaic/agent/MosaicHooks")) {
                                found.add(mname);
                            }
                        }
                    };
                }
            }, 0);
            for (int i = 1; i < e.length; i++) {
                boolean hit = found.contains(e[i]);
                allOk &= hit;
                System.out.println((hit ? "OK  " : "MISS") + " " + cls + " -> " + e[i]);
            }
            new ClassReader(out);   // 输出字节可重读(帧冒烟)
        }
        jar.close();
        System.out.println(allOk ? "ALL INJECTED" : "INJECTION GAP");
        if (!allOk) System.exit(1);
    }
}
