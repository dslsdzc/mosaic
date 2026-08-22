package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicTransaction;
import mosaic.runtime.MosaicTxResult;

/** 事务实现(补丁 pack 滚动更新):直通 mosaic_tx_*。begin 即完成 begin 级校验;
 *  prepare = 无操作成功(C begin 已校验);终态(commit/rollback/abort)后必须
 *  txFree(C:free 是唯一释放入口,abort 不释放句柄)。 */
public final class TxImpl implements MosaicTransaction {
    private long tx;

    private TxImpl(long tx) { this.tx = tx; }

    static MosaicTransaction begin(long rt, String patchPath) {
        if (patchPath == null || patchPath.isEmpty())
            throw new MosaicHandleException("tx patch path required");
        long h = Native.txBegin(rt, patchPath);
        if (h == 0)
            throw new MosaicHandleException("tx begin failed (lastError=" + Native.lastError(rt) + ")");
        return new TxImpl(h);
    }

    public MosaicTxResult prepare() { return new TxResultImpl(true, null); }

    public MosaicTxResult validate() {
        return Native.txValidate(tx) == 0 ? new TxResultImpl(true, null)
                                          : new TxResultImpl(false, "tx validate failed");
    }

    public MosaicTxResult commit() {
        int rc = Native.txCommit(tx);
        long h = tx; tx = 0;                 /* 终态:句柄不再可用 */
        Native.txFree(h);
        return rc == 0 ? new TxResultImpl(true, null)
                       : new TxResultImpl(false, "tx commit failed");
    }

    public MosaicTxResult rollback() {
        int rc = Native.txRollback(tx);
        long h = tx; tx = 0;
        Native.txFree(h);
        return rc == 0 ? new TxResultImpl(true, null)
                       : new TxResultImpl(false, "tx rollback failed");
    }

    public void abort() {
        if (tx == 0) return;
        long h = tx; tx = 0;
        Native.txAbort(h);
        Native.txFree(h);
    }

    static final class TxResultImpl implements MosaicTxResult {
        private final boolean ok;
        private final String error;
        TxResultImpl(boolean ok, String error) {
            this.ok = ok;
            this.error = error;
        }
        public boolean ok() { return ok; }
        public String error() { return error; }
    }
}
