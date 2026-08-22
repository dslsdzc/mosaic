package mosaic.runtime;

import mosaic.Since;

public interface MosaicWorkingSetStats {
    long totalMaterialized();
    long totalTombstoned();
    long totalRestored();
}
