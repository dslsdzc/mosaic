package mosaic.runtime.internal;

import mosaic.runtime.MosaicFunctionState;
import mosaic.runtime.MosaicStateStore;

/** 状态存储实现(M6-B):forFn(fnId) → 函数状态句柄。简单起见每次新建(不缓存
 *  per-fnId):状态读写在 native 侧即时生效,缓存仅省一次对象分配,无一致性
 *  收益;若未来热点化可换 ConcurrentHashMap<Long, FunctionStateImpl> 缓存。
 */
public final class StateStoreImpl implements MosaicStateStore {
    private final RuntimeImpl rt;

    StateStoreImpl(RuntimeImpl rt) { this.rt = rt; }

    public MosaicFunctionState forFn(long fnId) {
        return new FunctionStateImpl(rt);
    }
}
