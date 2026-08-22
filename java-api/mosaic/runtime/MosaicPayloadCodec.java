package mosaic.runtime;

public interface MosaicPayloadCodec {
    byte[] encodeInts(int... values);
    int[] decodeInts(byte[] payload);
}
