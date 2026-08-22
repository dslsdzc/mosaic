package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventPayload {
    /** 静态工厂:按事件域(eventId → 目录名前缀)把 byte[] 小端解码为字段;
     *  失败(长度与域不符/字节数非 4 倍数/eventId 未注册)抛 MosaicHandleException。 */
    @Since(1)
    static MosaicEventPayload of(int eventId, byte[] raw) {
        return mosaic.runtime.internal.EventPayloadImpl.of(eventId, raw);
    }
    /** 类型化解码:按事件域解码 byte[] → 字段;失败抛 MosaicHandleException。 */
    int[] decodeInts();
    /** 编码:字段 → byte[](小端,回环 of(id, b).encode() == b)。 */
    byte[] encode();
}
