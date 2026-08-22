package mosaic.runtime;

import mosaic.Since;

/** pack 只读信息(冷态)。 */
public interface MosaicPackInfo {
    long moduleCount();
    long functionCount();
    long triggerCount();
    long itemCount();
    int eventCount();
}
