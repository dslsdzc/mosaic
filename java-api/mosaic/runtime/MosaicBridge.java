package mosaic.runtime;

/** Java↔C 通道(内核桥;内部实现用,API 面保留以支持自诊断)。 */
public interface MosaicBridge {
    long nativeHandle();
    int lastError();
}
