package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventSubscription {
    int eventId();
    void close();
}
