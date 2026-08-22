package mosaic.runtime;

public interface MosaicCheckpoint {
    /** 取消/暂停时保存状态。 */
    void save(MosaicTask task);
}
