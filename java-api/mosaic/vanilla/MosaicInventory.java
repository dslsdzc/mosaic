package mosaic.vanilla;

/** 容器:槽位/计数/移动。 */
public interface MosaicInventory {
    int slotCount();
    MosaicInventorySlot slot(int index);
    /** 槽位内容物品(空槽返回 null)。 */
    MosaicItemStack getItem(int index);
    void setItem(int index, MosaicItemStack stack);
    int size();
}
