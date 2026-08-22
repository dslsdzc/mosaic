package mosaic.runtime;

import mosaic.Since;

/** 事务(滚动更新):prepare/validate/commit/rollback/abort。 */
public interface MosaicTransaction {
    MosaicTxResult prepare();
    MosaicTxResult validate();
    MosaicTxResult commit();
    MosaicTxResult rollback();
    void abort();
    /** 补丁 pack 信息(packPath + 补丁 fn id 列表;begin 时快照,终态后仍可读)。 */
    @Since(1)
    MosaicTxPatch patch();
}
