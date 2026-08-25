// bench/mc_client/McClient.java
// Mosaic E2E 最小 1.20.1 客户端(协议号 763,纯 Java 零依赖):
//   NIO SocketChannel(阻塞)+ 手写 VarInt/String/帧编解码 + zlib(java.util.zip)。
//   流程:握手(next state 2)→ LoginStart → LoginSuccess → 直接进 play(1.20.1 无
//   配置阶段)→ KeepAlive 回发 / 传送确认+回位置 / 聊天命令 / 聊天消息 / 实体
//   spawn 观测 → 8.5s 后主动断连(play 阶段无客户端断连包,关闭 socket 即断开,
//   服务端记录 lost connection)。
//
// 协议字段语义(逐包;全部经服务端 jar 字节码 javap 实测核实,见
// .superpowers/sdd/task-2-report.md §0):
//   - 握手 ClientIntentionPacket(serverbound 0x00,next state 2 = login)
//   - LoginStart ServerboundHelloPacket(login 0x00):String 名(≤16)+ bool(无
//     配置文件公钥)——1.20.1 无 profile UUID 字段(javap 实测构造函数仅两参)
//   - LoginSuccess ClientboundGameProfilePacket(login 0x02):String uuid +
//     String 名 + 属性列表(只解析前两项,帧剩余跳过)
//   - SetCompression ClientboundLoginCompressionPacket(login 0x03):VarInt
//     阈值;之后双方向帧 = VarInt(未压缩长度,0=原始)+ 数据
//   - play 包 id(自 ConnectionProtocol 静态初始化字节码实测):
//       serverbound:0x00 ConfirmTeleportation(VarInt)/0x04 ChatCommand/
//       0x05 ChatMessage/0x08 ClientInformation/0x12 KeepAlive(long)/
//       0x14 MovePlayerPos(3×double + u8 onGround)
//       clientbound:0x01 AddEntity/0x1a Disconnect/0x23 KeepAlive(long)/
//       0x28 Login(整帧跳过)/0x3c PlayerPositionAndLook/0x64 SystemChat
//   - ChatCommand 体:String 命令(无前导 '/',vanilla 客户端发送前剥离;
//     performCommand(ParseResults,String) 注入 hook 收到即此串)+ Instant
//     (单个 long = epoch 毫秒;sf.v()=readInstant 实测
//     Instant.ofEpochMilli(readLong()))+ long salt +
//     ArgumentSignatures(VarInt 0)+ LastSeenMessages$Update(VarInt 0 offset +
//     固定 3 字节空位图)——位图定长 (20+8)/8=3 字节(writeBitSet 实测
//     Arrays.copyOf(toByteArray,3),无长度前缀);空位图 = "无已确认消息",
//     LastSeenMessagesValidator 接受(字节码实测)
//   - ChatMessage 体:同 ChatCommand 但签名位 = bool false(无签名;
//     enforce-secure-profile=false 下服务端接受,见报告 §2.2)
//   - PlayerPositionAndLook(xa)体:3×double + 2×float + u8 flags + VarInt
//     teleportId;客户端回 ConfirmTeleportation(id) + MovePlayerPos(原坐标,
//     与传送位置一致 → "moved wrongly" 校验零位移通过)
//   - AddEntity(us)体:VarInt entityId + UUID + VarInt type(注册表 id)+
//     3×double + 3×byte + VarInt data + 3×short(velocity)
//
// 日志:stdout。[SEND]/[RECV] 行 = 每帧包 id + 帧体字节数 + 小帧 hex;
// 服务端 packet_received/sent 的 size_hint 与此逐帧对照(证据交叉验证)。
// 命令/维度 hash 预打印(FNV-1a-32,与 agent 端 cmd_hash/dimension 同算法,
// 可复核)。
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.UUID;
import java.util.zip.Inflater;

public class McClient {

    /* ---- 协议常量(1.20.1) ----
       协议号 763(非 765!):官方 1.20.1 server jar 的 version.json 实测
       protocol_version=763(sha1 84194a2f… 与 piston-meta 官方下载一致;Mojang
       1.20.1 未把 1.20 的协议号 763 升为 765)。服务端 handshake 检查
       (aiz.a(abb)=handleIntention)按 SharedConstants.getProtocolVersion()
       (=version.json 值)与握手包协议号比对:763 → 通过,765 → 断线
       "multiplayer.disconnect.incompatible"(E2E 实测;wiki.vg 所列 765 与
       本 jar 实测不符,以实测为准;T1/T6 状态 ping 亦用 763)。 */
    static final int PROTOCOL = 763;
    /* login 阶段 id(双版本稳定) */
    static final int LB_LOGIN_DISCONNECT = 0x00;
    static final int LB_LOGIN_SUCCESS = 0x02;
    static final int LB_SET_COMPRESSION = 0x03;
    /* serverbound play(自服务端 jar 实测) */
    static final int SB_CONFIRM_TELEPORT = 0x00;   /* VarInt teleportId */
    static final int SB_CHAT_COMMAND = 0x04;       /* 命令串无前导 '/' */
    static final int SB_CHAT_MESSAGE = 0x05;
    static final int SB_CLIENT_INFO = 0x08;
    static final int SB_KEEP_ALIVE = 0x12;         /* long 原样回显 */
    static final int SB_MOVE_POS = 0x14;           /* 3×double + u8 onGround */
    /* clientbound play(自服务端 jar 实测) */
    static final int CB_SPAWN_ENTITY = 0x01;
    static final int CB_DISCONNECT = 0x1a;
    static final int CB_KEEP_ALIVE = 0x23;
    static final int CB_LOGIN = 0x28;              /* JoinGame:整帧跳过 */
    static final int CB_POSITION = 0x3c;
    static final int CB_SYSTEM_CHAT = 0x64;

    /* ---- 动作序列(play 进入后毫秒偏移) ---- */
    static final long T_CLIENT_INFO = 500;
    static final long T_STATUS_0 = 1500;           /* /mosaic status 基线 */
    static final long T_SUMMON = 3000;             /* /summon sheep:player_command + entity_spawn */
    static final long T_CHAT = 4500;               /* 聊天消息:player_chat */
    static final long T_STATUS_1 = 6500;           /* /mosaic status 终态 */
    static final long T_CLOSE = 8500;              /* 优雅断连(直接关 socket) */
    static final long T_TIMEOUT = 30000;

    static final String HOST_ARG = "127.0.0.1";
    static final int PORT_ARG = 25565;
    static final String USER_ARG = "MosaicBot";

    SocketChannel ch;
    boolean compressed = false;                    /* SetCompression 后帧格式 */
    Inflater inflater = new Inflater();
    long playStart = -1;
    boolean closed = false;
    boolean[] sent = new boolean[8];               /* 动作去重 */

    /* 收到的字节积累器(帧级解析;1 MiB 上限防整帧内存放大) */
    byte[] in = new byte[1 << 20];
    int inPos = 0, inLen = 0;

    public static void main(String[] args) throws Exception {
        String host = args.length > 0 ? args[0] : HOST_ARG;
        int port = args.length > 1 ? Integer.parseInt(args[1]) : PORT_ARG;
        String user = args.length > 2 ? args[2] : USER_ARG;
        /* 证据哈希(与 agent 端同算法,预先打印供报告复核) */
        System.out.println("[HASH] fnv1a32(\"summon minecraft:sheep\") = 0x"
                + Integer.toHexString(fnv1a32("summon minecraft:sheep")));
        System.out.println("[HASH] fnv1a32(\"mosaic status\") = 0x"
                + Integer.toHexString(fnv1a32("mosaic status")));
        System.out.println("[HASH] fnv1a32(\"minecraft:overworld\") = 0x"
                + Integer.toHexString(fnv1a32("minecraft:overworld")));
        System.out.println("[HASH] fnv1a32(\"hello from mosaic-client e2e\") = 0x"
                + Integer.toHexString(fnv1a32("hello from mosaic-client e2e")));
        new McClient().run(host, port, user);
    }

    void run(String host, int port, String user) throws Exception {
        System.out.println("[CLIENT] connecting " + host + ":" + port + " as " + user
                + " (protocol " + PROTOCOL + ")");
        ch = SocketChannel.open(new InetSocketAddress(host, port));
        ch.configureBlocking(true);

        /* ---- 握手:ClientIntentionPacket(handshake 0x00,next state 2) ---- */
        ByteArrayOutputStream hs = new ByteArrayOutputStream();
        writeVarInt(hs, PROTOCOL);
        writeString(hs, host);
        hs.write((port >> 8) & 0xFF);
        hs.write(port & 0xFF);
        writeVarInt(hs, 2);                        /* next state = login */
        send(0x00, hs.toByteArray(), "Handshake");

        /* ---- LoginStart:ServerboundHelloPacket(login 0x00) ---- */
        ByteArrayOutputStream ls = new ByteArrayOutputStream();
        writeString(ls, user);
        ls.write(0);                               /* bool:无配置文件公钥(离线) */
        send(0x00, ls.toByteArray(), "LoginStart");

        /* ---- 登录阶段:LoginSuccess / SetCompression / Disconnect ---- */
        long loginDeadline = System.currentTimeMillis() + 20000;
        boolean loggedIn = false;
        while (!loggedIn && !closed) {
            Frame f = readFrame();                 /* 阻塞读(登录阶段) */
            if (f == null) break;
            byte[] p = f.payload;
            switch (f.id) {
                case LB_LOGIN_SUCCESS:             /* 16B UUID + String 名 + 属性(javap 实测
                                                      writeGameProfile:writeUUID + writeUtf) */
                    int[] pos = {0};
                    UUID uuid = readUuid(p, pos);
                    String name = readString(p, pos);
                    System.out.println("[LOGIN] LoginSuccess uuid=" + uuid + " name=" + name);
                    loggedIn = true;
                    break;
                case LB_SET_COMPRESSION:           /* VarInt 阈值 */
                    int thr = readVarInt(p, new int[1]);
                    compressed = true;
                    System.out.println("[LOGIN] SetCompression threshold=" + thr
                            + " (zlib frames enabled)");
                    break;
                case LB_LOGIN_DISCONNECT:          /* String 原因 */
                    System.out.println("[LOGIN] server rejected login: "
                            + readString(p, new int[1]));
                    closed = true;
                    break;
                default:
                    System.out.println("[LOGIN] unexpected id=0x" + Integer.toHexString(f.id)
                            + " size=" + f.payload.length + (f.payload.length <= 64 ? " body=" + hex(f.payload) : "")
                            + " (ignored)");
            }
            if (System.currentTimeMillis() > loginDeadline) {
                System.out.println("[CLIENT] login timeout");
                closed = true;
            }
        }
        if (!loggedIn && !closed) {
            System.out.println("[CLIENT] login failed (no LoginSuccess)");
            closed = true;
        }
        if (!closed) {
            System.out.println("[PLAY] entered play stage (buf inPos=" + inPos
                    + " inLen=" + inLen + ")");
            playStart = System.currentTimeMillis();
        }

        /* ---- play 阶段:非阻塞轮询(KeepAlive/传送/实体/断开 + 定时动作) ---- */
        ch.configureBlocking(false);
        while (!closed) {
            long now = System.currentTimeMillis();
            /* 定时动作(play 进入后偏移) */
            if (playStart >= 0) {
                if (!sent[0] && now - playStart >= T_CLIENT_INFO) { sent[0] = true; sendClientInfo(); }
                if (!sent[1] && now - playStart >= T_STATUS_0)     { sent[1] = true; sendChatCommand("mosaic status"); }
                if (!sent[2] && now - playStart >= T_SUMMON)       { sent[2] = true; sendChatCommand("summon minecraft:sheep"); }
                if (!sent[3] && now - playStart >= T_CHAT)         { sent[3] = true; sendChatMessage("hello from mosaic-client e2e"); }
                if (!sent[4] && now - playStart >= T_STATUS_1)     { sent[4] = true; sendChatCommand("mosaic status"); }
                if (!sent[5] && now - playStart >= T_CLOSE)        { sent[5] = true; closeGracefully(); }
            }
            if (closed) break;                   /* closeGracefully 后不再读 */
            if (now - playStart > T_TIMEOUT) {
                System.out.println("[CLIENT] overall timeout");
                System.exit(2);
            }
            /* 读可用字节 */
            int n = ch.read(ByteBuffer.wrap(in, inLen, in.length - inLen));
            if (n < 0) {                           /* 对端关闭 */
                System.out.println("[CLIENT] server closed connection");
                closed = true;
                break;
            }
            inLen += n;
            try {
                parseFrames();
            } catch (IOException e) {
                System.out.println("[CLIENT] frame error: " + e);
                System.exit(1);
            }
            if (inPos == inLen) inPos = inLen = 0;
            else if (inPos > 0) {
                System.arraycopy(in, inPos, in, 0, inLen - inPos);
                inLen -= inPos;
                inPos = 0;
            }
            try { Thread.sleep(10); } catch (InterruptedException e) { break; }
        }
        System.out.println("[CLIENT] done");
    }

    /* 从积累器解析 0..N 个完整帧;不完整(帧头/帧体缺字节)则保留等待 */
    void parseFrames() throws IOException {
        while (true) {
            int[] pos = {inPos};
            int frameLen = peekVarInt(in, pos, inLen); /* 帧头 VarInt(不消费) */
            if (frameLen < 0) break;               /* 帧头不完整 */
            if (inLen - pos[0] < frameLen) break;  /* 帧体不完整 */
            byte[] frame = Arrays.copyOfRange(in, pos[0], pos[0] + frameLen);
            inPos = pos[0] + frameLen;
            handleFrame(frame);
        }
    }

    /* 帧:VarInt 帧长 + 帧体;压缩开启时帧体 = VarInt(0=原始|长度)+ 数据 */
    void handleFrame(byte[] frame) {
        try {
            int[] pos = {0};
            byte[] body;
            if (compressed) {
                int dataLen = readVarInt(frame, pos);
                if (dataLen == 0) {
                    body = Arrays.copyOfRange(frame, pos[0], frame.length);
                } else {
                    /* dataLen = 未压缩长度(非 zlib 输入长度!vanilla
                       PacketDecompressor 语义:输入 = 帧剩余全部字节,
                       inflate 后校验长度 == dataLen——E2E 实测 JoinGame
                       dataLen=39289 而帧仅 4632 字节) */
                    body = inflate(frame, pos[0], frame.length - pos[0]);
                    if (body.length != dataLen) {
                        System.out.println("[CLIENT] WARN inflate " + body.length
                                + " != dataLen " + dataLen);
                    }
                }
            } else {
                body = frame;
            }
            /* 包 id 从头读(pos 已被 dataLen VarInt 推进,不能复用) */
            int id = readVarInt(body, new int[1]);
            int[] r = {0};
            readVarInt(body, r);
            byte[] p = Arrays.copyOfRange(body, r[0], body.length);
            String nm = cbName(id);
            System.out.println("[RECV] " + nm + " id=0x" + Integer.toHexString(id)
                    + " size=" + p.length + (p.length <= 32 ? " body=" + hex(p) : ""));
            switch (id) {
                case CB_KEEP_ALIVE: {              /* long 回显 */
                    long ka = readLong(p, new int[1]);
                    ByteArrayOutputStream b = new ByteArrayOutputStream();
                    writeLong(b, ka);
                    send(SB_KEEP_ALIVE, b.toByteArray(), "KeepAlive");
                    break;
                }
                case CB_POSITION: {                /* 传送确认 + 回位置 */
                    int[] q = {0};
                    double x = readDouble(p, q), y = readDouble(p, q), z = readDouble(p, q);
                    float yaw = readFloat(p, q), pitch = readFloat(p, q);
                    int flags = p[q[0]++] & 0xFF;
                    int tid = readVarInt(p, q);
                    System.out.println("[PLAY] position x=" + x + " y=" + y + " z=" + z
                            + " yaw=" + yaw + " pitch=" + pitch + " flags=" + flags
                            + " teleportId=" + tid);
                    ByteArrayOutputStream c = new ByteArrayOutputStream();
                    writeVarInt(c, tid);
                    send(SB_CONFIRM_TELEPORT, c.toByteArray(), "ConfirmTeleportation");
                    ByteArrayOutputStream m = new ByteArrayOutputStream();
                    writeDouble(m, x); writeDouble(m, y); writeDouble(m, z);
                    m.write(1);                    /* onGround = true(传送落点) */
                    send(SB_MOVE_POS, m.toByteArray(), "MovePlayerPos");
                    break;
                }
                case CB_SPAWN_ENTITY: {            /* VarInt entityId + UUID + VarInt type + ... */
                    int[] q = {0};
                    int eid = readVarInt(p, q);
                    long hi = readLong(p, q), lo = readLong(p, q);
                    UUID uuid = new UUID(hi, lo);
                    int type = readVarInt(p, q);
                    double x = readDouble(p, q), y = readDouble(p, q), z = readDouble(p, q);
                    System.out.println("[PLAY] spawned entity entityId=" + eid + " type=" + type
                            + " uuid=" + uuid + " at (" + x + ", " + y + ", " + z + ")");
                    break;
                }
                case CB_DISCONNECT: {              /* String 原因(踢出) */
                    System.out.println("[PLAY] server disconnected us: "
                            + readString(p, new int[1]));
                    closed = true;
                    System.exit(3);
                    break;
                }
                case CB_SYSTEM_CHAT: {             /* 命令反馈(如 Unknown command) */
                    String s = readString(p, new int[1]);
                    System.out.println("[PLAY] systemChat: " + s);
                    break;
                }
                default:
                    break;                         /* 其余(区块/NBT 等)整帧跳过 */
            }
        } catch (IOException e) {
            System.out.println("[CLIENT] frame error: " + e);
            closed = true;
        }
    }

    void sendClientInfo() throws IOException {
        /* ServerboundClientInformationPacket(0x08):String 语言 + u8 视距 +
           VarInt 聊天模式(FULL=0)+ bool 颜色 + u8 皮肤 + VarInt 主手 + bool×2 */
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        writeString(b, "en_us");
        b.write(12);                               /* view distance */
        writeVarInt(b, 0);                         /* chat visibility FULL */
        b.write(1);                                /* chat colors */
        b.write(0x7F);                             /* skin parts 全 */
        writeVarInt(b, 0);                         /* main hand RIGHT */
        b.write(0);                                /* text filtering */
        b.write(1);                                /* allows listing */
        send(SB_CLIENT_INFO, b.toByteArray(), "ClientInformation");
    }

    void sendChatCommand(String cmd) throws IOException {
        /* ChatCommand(0x04):String(无 '/')+ Instant + long salt +
           ArgumentSignatures 空(VarInt 0)+ LastSeen 空更新(VarInt 0, VarInt 0)。
           Instant 编码 = 单个 long(epoch 毫秒)——sf.v()=readInstant 实测
           Instant.ofEpochMilli(readLong())(E2E 首轮错编为秒+纳秒 VarInt 被
           服务端 "found 2 bytes extra" 踢出) */
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        writeString(b, cmd);
        writeLong(b, System.currentTimeMillis());  /* Instant = epoch millis */
        writeLong(b, 0);                           /* salt */
        writeVarInt(b, 0);                         /* ArgumentSignatures 空列表 */
        writeVarInt(b, 0);                         /* lastSeen offset */
        b.write(new byte[3]);                      /* lastSeen 空位图 = 固定 3 字节
                                                      (ceil(20/8);writeBitSet 按
                                                      (maxBits+8)/8 定长,非 VarInt) */
        send(SB_CHAT_COMMAND, b.toByteArray(), "ChatCommand \"" + cmd + "\"");
    }

    void sendChatMessage(String msg) throws IOException {
        /* ChatMessage(0x05):String + Instant(long epoch millis)+ long salt +
           bool(无签名)+ LastSeen 空 */
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        writeString(b, msg);
        writeLong(b, System.currentTimeMillis());  /* Instant = epoch millis */
        writeLong(b, 0);                           /* salt */
        b.write(0);                                /* 无消息签名(离线) */
        writeVarInt(b, 0);                         /* lastSeen offset */
        b.write(new byte[3]);                      /* lastSeen 空位图 = 固定 3 字节 */
        send(SB_CHAT_MESSAGE, b.toByteArray(), "ChatMessage \"" + msg + "\"");
    }

    void closeGracefully() {
        /* play 阶段无 serverbound 断开包;直接关 socket(服务端记 lost connection) */
        System.out.println("[CLIENT] graceful close (socket close, no serverbound disconnect in play)");
        try { ch.close(); } catch (IOException e) { /* ignore */ }
        closed = true;
    }

    /* ---- 帧收发 ---- */
    void send(int id, byte[] payload, String name) throws IOException {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        writeVarInt(body, id);
        body.write(payload, 0, payload.length);
        byte[] b = body.toByteArray();
        ByteArrayOutputStream frame = new ByteArrayOutputStream();
        if (compressed) writeVarInt(frame, 0);     /* 未压缩标志(全部包 < 256B) */
        frame.write(b, 0, b.length);
        byte[] f = frame.toByteArray();
        ByteArrayOutputStream wire = new ByteArrayOutputStream();
        writeVarInt(wire, f.length);
        wire.write(f, 0, f.length);
        byte[] w = wire.toByteArray();
        ByteBuffer buf = ByteBuffer.wrap(w);
        while (buf.hasRemaining()) ch.write(buf);
        System.out.println("[SEND] " + name + " id=0x" + Integer.toHexString(id)
                + " size=" + b.length + (b.length <= 32 ? " body=" + hex(b) : ""));
    }

    /* 阻塞读一帧(登录阶段用);返回 {id, payload} 或 null(关闭/超时由调用方判) */
    Frame readFrame() throws IOException {
        int[] pos = {0};
        int frameLen = -1;
        while (frameLen < 0) {
            int n = ch.read(ByteBuffer.wrap(in, inLen, in.length - inLen));
            if (n < 0) return null;
            inLen += n;
            pos[0] = inPos;
            frameLen = peekVarInt(in, pos, inLen);
            if (frameLen < 0 && inLen - inPos > 5) throw new IOException("bad frame header");
        }
        while (inLen - pos[0] < frameLen) {
            int n = ch.read(ByteBuffer.wrap(in, inLen, in.length - inLen));
            if (n < 0) return null;
            inLen += n;
        }
        byte[] frame = Arrays.copyOfRange(in, pos[0], pos[0] + frameLen);
        inPos = pos[0] + frameLen;
        if (inPos == inLen) inPos = inLen = 0;
        int[] q = {0};
        byte[] body;
        if (compressed) {
            int dataLen = readVarInt(frame, q);
            if (dataLen == 0) {
                body = Arrays.copyOfRange(frame, q[0], frame.length);
            } else {
                /* dataLen = 未压缩长度;zlib 输入 = 帧剩余全部字节 */
                body = inflate(frame, q[0], frame.length - q[0]);
                if (body.length != dataLen) {
                    System.out.println("[CLIENT] WARN inflate " + body.length
                            + " != dataLen " + dataLen);
                }
            }
        } else {
            body = frame;
        }
        /* 包 id 从头读(不能用已推进的 q——dataLen VarInt 偏移会跳一字节) */
        int id = readVarInt(body, new int[1]);
        int[] r = {0};
        readVarInt(body, r);
        byte[] p = Arrays.copyOfRange(body, r[0], body.length);
        System.out.println("[RECV] " + cbName(id) + " id=0x" + Integer.toHexString(id)
                + " size=" + p.length);
        return new Frame(id, p);
    }

    static String cbName(int id) {
        switch (id) {
            case CB_SPAWN_ENTITY: return "SpawnEntity";
            case CB_DISCONNECT: return "Disconnect";
            case CB_KEEP_ALIVE: return "KeepAlive";
            case CB_LOGIN: return "Login";
            case CB_POSITION: return "PlayerPositionAndLook";
            case CB_SYSTEM_CHAT: return "SystemChat";
            default: return "pkt";
        }
    }

    /* ---- 编解码(VarInt/String/数值;协议字段语义见头注释) ---- */
    static void writeVarInt(ByteArrayOutputStream out, int v) {
        while ((v & ~0x7F) != 0) { out.write((v & 0x7F) | 0x80); v >>>= 7; }
        out.write(v);
    }

    static int readVarInt(byte[] b, int[] pos) {
        int v = 0, shift = 0;
        while (true) {
            int x = b[pos[0]++] & 0xFF;
            v |= (x & 0x7F) << shift;
            if ((x & 0x80) == 0) return v;
            shift += 7;
            if (shift > 35) throw new IllegalStateException("bad varint");
        }
    }

    /* 非消费扫描:返回帧头 VarInt;字节不足返回 -1(帧头本身不完整)。
       必须按有效数据长度 end 限定扫描范围——越过 end 会读到积累器的陈旧
       字节,VarInt 跨读包时被陈旧字节"补全"成错误小帧长 → 帧边界错位
       (E2E 实测:JoinGame 帧 dataLen=39289 崩溃)。 */
    static int peekVarInt(byte[] b, int[] pos, int end) {
        int v = 0, shift = 0;
        for (int i = pos[0]; i < end && i < pos[0] + 5; i++) {
            int x = b[i] & 0xFF;
            v |= (x & 0x7F) << shift;
            if ((x & 0x80) == 0) { pos[0] = i + 1; return v; }
            shift += 7;
        }
        return -1;
    }

    static void writeString(ByteArrayOutputStream out, String s) {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        writeVarInt(out, b.length);
        out.write(b, 0, b.length);
    }

    /* zlib 解压(dataLen 字节 → 解压后帧体) */
    byte[] inflate(byte[] src, int off, int dataLen) throws IOException {
        try {
            inflater.reset();
            inflater.setInput(src, off, dataLen);
            byte[] out = new byte[1 << 20];
            int n = inflater.inflate(out);
            return Arrays.copyOf(out, n);
        } catch (java.util.zip.DataFormatException e) {
            throw new IOException("inflate failed", e);
        }
    }

    static String readString(byte[] b, int[] pos) {
        int len = readVarInt(b, pos);
        String s = new String(b, pos[0], len, StandardCharsets.UTF_8);
        pos[0] += len;
        return s;
    }

    static void writeLong(ByteArrayOutputStream out, long v) {
        for (int i = 0; i < 8; i++) out.write((int) (v >>> (56 - 8 * i)));
    }

    static long readLong(byte[] b, int[] pos) {
        long v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (b[pos[0]++] & 0xFF);
        return v;
    }

    /* UUID = 16 原始字节(大端;协议标准,1.16.2 起 LoginSuccess/AddEntity 均此) */
    static UUID readUuid(byte[] b, int[] pos) {
        long hi = readLong(b, pos), lo = readLong(b, pos);
        return new UUID(hi, lo);
    }

    static void writeDouble(ByteArrayOutputStream out, double d) {
        long bits = Double.doubleToLongBits(d);
        for (int i = 0; i < 8; i++) out.write((int) (bits >>> (56 - 8 * i)));
    }

    static double readDouble(byte[] b, int[] pos) {
        long v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (b[pos[0]++] & 0xFF);
        return Double.longBitsToDouble(v);
    }

    static float readFloat(byte[] b, int[] pos) {
        int v = 0;
        for (int i = 0; i < 4; i++) v = (v << 8) | (b[pos[0]++] & 0xFF);
        return Float.intBitsToFloat(v);
    }

    static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format("%02x", x));
        return sb.toString();
    }

    /* FNV-1a-32:与 agent 端 fnv1a32 同算法(命令文本/维度串共用,可复核) */
    static int fnv1a32(String s) {
        int h = 0x811c9dc5;
        for (int i = 0; i < s.length(); i++) {
            h ^= s.charAt(i);
            h *= 0x01000193;
        }
        return h;
    }

    static final class Frame { final int id; final byte[] payload; Frame(int id, byte[] p) { this.id = id; this.payload = p; } }
}
