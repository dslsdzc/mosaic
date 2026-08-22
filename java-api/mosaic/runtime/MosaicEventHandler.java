package mosaic.runtime;

import mosaic.Since;

@FunctionalInterface
public interface MosaicEventHandler {
    void onEvent(int eventId, byte[] payload);
}
