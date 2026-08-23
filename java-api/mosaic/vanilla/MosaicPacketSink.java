package mosaic.vanilla;

import mosaic.Since;

/** 网络包回调(网络域,Task 7):接收 MosaicPacket 稳定投影。
 *  经 MosaicPacketListener.onPacket(MosaicPacketSink) 注册,返回可关闭订阅。 */
@Since(1)
@FunctionalInterface
public interface MosaicPacketSink {
    void onPacket(MosaicPacket packet);
}
