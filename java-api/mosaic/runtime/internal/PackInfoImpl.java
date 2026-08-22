package mosaic.runtime.internal;

import mosaic.runtime.MosaicPackInfo;

/** pack 只读信息(冷态):5 计数经 packInfoCount 一次 native 返回
 *  (0=module 1=fn 2=trigger 3=item 4=event;全部基础 pack 合并求和)。 */
public final class PackInfoImpl implements MosaicPackInfo {
    private final RuntimeImpl rt;

    PackInfoImpl(RuntimeImpl rt) { this.rt = rt; }

    public long moduleCount() { return Native.packInfoCount(rt.handle(), 0); }
    public long functionCount() { return Native.packInfoCount(rt.handle(), 1); }
    public long triggerCount() { return Native.packInfoCount(rt.handle(), 2); }
    public long itemCount() { return Native.packInfoCount(rt.handle(), 3); }
    public int eventCount() { return (int) Native.packInfoCount(rt.handle(), 4); }
}
