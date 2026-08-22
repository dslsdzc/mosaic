package mosaic.vanilla;

import mosaic.Since;

public interface MosaicBlockPos {
    int x(); int y(); int z();
    static MosaicBlockPos of(int x, int y, int z) {
        return new MosaicBlockPos() {
            public int x() { return x; } public int y() { return y; } public int z() { return z; } };
    }
}
