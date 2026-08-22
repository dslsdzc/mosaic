package mosaic.runtime;

/** 事务(滚动更新):prepare/validate/commit/rollback/abort。 */
public interface MosaicTransaction {
    MosaicTxResult prepare();
    MosaicTxResult validate();
    MosaicTxResult commit();
    MosaicTxResult rollback();
    void abort();
}
