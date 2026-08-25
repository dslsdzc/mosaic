package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventDispatcher {
    /** 派发:C 内核订阅者执行 + Java 侧订阅者执行;返回执行总数。 */
    int dispatch(int eventId, byte[] payload);
    /** Java 侧运行时订阅(回调表,纯 Java 层)。 */
    MosaicEventSubscription subscribe(int eventId, MosaicEventHandler handler);
    void unsubscribe(MosaicEventSubscription subscription);

    /** Java 侧事件监听器(观测通道,Task 3):派发返回后广播
     *  (事件/执行数/载荷;语义见 {@link MosaicEventListener});监听器异常
     *  隔离、重入 depth guard,均不影响派发返回。返回注销句柄。 */
    @Since(1)
    MosaicEventSubscription addEventListener(int eventId, MosaicEventListener listener);
    /** 注销事件监听器(接受 addEventListener 返回的句柄;非本类句柄 → 空操作)。 */
    @Since(1)
    void removeEventListener(MosaicEventSubscription subscription);
}
