package mosaic.runtime;

import mosaic.Since;

public interface MosaicWorkingSet {
    int count();
    /** 驱逐空闲函数(窗口内未使用);返回墓碑数。 */
    int evictIdle(long windowNanos);
    /** 全部 ACTIVE 函数 id(快照)。 */
    long[] activeFnIds();
}
