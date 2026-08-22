package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventDispatcher {
    /** 派发:C 内核订阅者执行 + Java 侧订阅者执行;返回执行总数。 */
    int dispatch(int eventId, byte[] payload);
    /** Java 侧运行时订阅(回调表,纯 Java 层)。 */
    MosaicEventSubscription subscribe(int eventId, MosaicEventHandler handler);
    void unsubscribe(MosaicEventSubscription subscription);
}
