package mosaic.vanilla;

import mosaic.Since;

public interface MosaicPacketListener {
    /** 注册包处理(按包类型名);返回订阅可关闭。 */
    AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler);

    /** 注册包回调(MosaicPacket 稳定投影,7.1);返回订阅可关闭(注销)。
     *  MosaicPacketSink.onPacket(MosaicPacket) 接收每次包投影;契约环境无真实
     *  连接收发包 → 不派发(不伪造);真实分发路径 = 服务端环境(内核
     *  packet_received/packet_sent 事件已由 Task 6 1.20.1 E2E 验证)。 */
    @Since(1)
    AutoCloseable onPacket(MosaicPacketSink sink);
}
