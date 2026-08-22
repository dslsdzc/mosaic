package mosaic.runtime.internal;

import mosaic.runtime.MosaicActivationGate;
import mosaic.runtime.MosaicActivationPolicy;

/** 激活策略门(纯 Java 策略持有;默认自动:事件驱动物化)。 */
public final class ActivationGateImpl implements MosaicActivationGate {
    private MosaicActivationPolicy policy = (fnId, eventId) -> true;

    public MosaicActivationPolicy policy() { return policy; }

    public void setPolicy(MosaicActivationPolicy policy) {
        if (policy != null) this.policy = policy;
    }
}
