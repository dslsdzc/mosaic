package mosaic.vanilla;

import mosaic.Since;

public interface MosaicItemStack {
    MosaicItem item();
    int count();
    MosaicItemStack copy();
}
