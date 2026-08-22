package mosaic.vanilla;

public interface MosaicInventorySlot {
    int index();
    MosaicItemStack item();
    boolean isEmpty();
}
