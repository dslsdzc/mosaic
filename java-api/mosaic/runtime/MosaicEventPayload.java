package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventPayload {
    /** 类型化解码:按事件域解码 byte[] → 字段;失败抛 MosaicHandleException。 */
    int[] decodeInts();
    byte[] encode();
}
