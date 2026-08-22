package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicPayloadCodec;

/** 载荷编解码工具:小端 u32 数组 ↔ byte[](与事件载荷 events.h 约定一致)。
 *  解码失败语义与事件载荷一致(§8):长度非 4 倍数 → MosaicHandleException。
 *  无状态单例(immutable)。 */
public final class PayloadCodecImpl implements MosaicPayloadCodec {

    private static final MosaicPayloadCodec LITTLE_ENDIAN = new PayloadCodecImpl();

    private PayloadCodecImpl() {}

    /** 小端编码单例工厂(接口 MosaicPayloadCodec.littleEndian() 委托于此)。 */
    public static MosaicPayloadCodec littleEndian() { return LITTLE_ENDIAN; }

    public byte[] encodeInts(int... values) {
        if (values == null) throw new NullPointerException("values");
        byte[] out = new byte[values.length * 4];
        for (int i = 0; i < values.length; i++) {
            int v = values[i], o = i * 4;
            out[o] = (byte) v;
            out[o + 1] = (byte) (v >>> 8);
            out[o + 2] = (byte) (v >>> 16);
            out[o + 3] = (byte) (v >>> 24);
        }
        return out;
    }

    public int[] decodeInts(byte[] payload) {
        if (payload == null) throw new NullPointerException("payload");
        if (payload.length % 4 != 0)
            throw new MosaicHandleException("payload length " + payload.length
                + " is not a multiple of 4");
        int[] out = new int[payload.length / 4];
        for (int i = 0; i < out.length; i++) {
            int o = i * 4;
            out[i] = (payload[o] & 0xff) | ((payload[o + 1] & 0xff) << 8)
                   | ((payload[o + 2] & 0xff) << 16) | ((payload[o + 3] & 0xff) << 24);
        }
        return out;
    }
}
