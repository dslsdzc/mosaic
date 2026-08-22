package mosaic.runtime;

import mosaic.Since;

public interface MosaicPayloadCodec {
    /** 小端编解码单例(与事件载荷 byte[] ↔ events.h 结构体约定一致)。 */
    @Since(1)
    static MosaicPayloadCodec littleEndian() {
        return mosaic.runtime.internal.PayloadCodecImpl.littleEndian();
    }
    /** 编码:u32 数组 → 小端 byte[](每值 4 字节)。 */
    byte[] encodeInts(int... values);
    /** 解码:小端 byte[] → u32 数组;长度非 4 倍数抛 MosaicHandleException。 */
    int[] decodeInts(byte[] payload);
}
