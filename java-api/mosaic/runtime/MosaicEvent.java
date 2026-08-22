package mosaic.runtime;

import mosaic.Since;

/** 事件:派发/订阅/目录/载荷。 */
public interface MosaicEvent {
    int eventId();
    String name();
    int payloadSize();
}
