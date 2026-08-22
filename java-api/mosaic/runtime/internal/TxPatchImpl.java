package mosaic.runtime.internal;

import java.util.Arrays;
import mosaic.runtime.MosaicTxPatch;

/** 补丁 pack 信息:packPath(由 TxImpl 构造时持有)+ fnIds(经 native 枚举
 *  补丁 pack fn 表;txPatchFnIds 两阶段:探测总数 → 填充)。 */
public final class TxPatchImpl implements MosaicTxPatch {
    private final String packPath;
    private final long[] fnIds;

    TxPatchImpl(String packPath, long tx) {
        this.packPath = packPath;
        int n = Native.txPatchFnIds(tx, null);
        if (n <= 0) { this.fnIds = new long[0]; return; }
        long[] out = new long[n];
        int w = Native.txPatchFnIds(tx, out);
        this.fnIds = w == n ? out : Arrays.copyOf(out, w);
    }

    public String packPath() { return packPath; }
    public long[] fnIds() { return fnIds; }
}
