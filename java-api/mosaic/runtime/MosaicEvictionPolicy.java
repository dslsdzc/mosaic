package mosaic.runtime;

import mosaic.Since;

/** 驱逐:窗口 T + 可插拔策略;refs>0 绝不驱逐。 */
public interface MosaicEvictionPolicy {
    /** 窗口纳秒(Denning T);0 = 立即过期。 */
    long windowNanos();
}
