package mosaic.runtime.internal;

import mosaic.runtime.MosaicEvictionPolicy;

/** 驱逐策略:持有窗口纳秒(Denning T;0 = 立即过期)。
 *  调用方把 windowNanos() 交给 workingSet().evictIdle(windowNanos) 使用。 */
public final class EvictionPolicyImpl implements MosaicEvictionPolicy {
    private final long windowNanos;

    public EvictionPolicyImpl(long windowNanos) { this.windowNanos = windowNanos; }

    public long windowNanos() { return windowNanos; }
}
