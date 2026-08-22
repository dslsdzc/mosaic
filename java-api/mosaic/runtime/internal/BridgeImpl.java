package mosaic.runtime.internal;

import mosaic.runtime.MosaicBridge;

/** 自诊断桥(Java↔C 内核通道):nativeHandle/lastError 直通 RuntimeImpl
 *  (同一 JNI 句柄与错误槽;API 面保留以支持自诊断与工具层)。无状态,线程安全。 */
public final class BridgeImpl implements MosaicBridge {
    private final RuntimeImpl rt;

    BridgeImpl(RuntimeImpl rt) { this.rt = rt; }

    public long nativeHandle() { return rt.handle(); }

    /** C 内核错误槽直读(与 MosaicRuntime.lastError() 同源)。 */
    public int lastError() { return rt.lastError(); }
}
