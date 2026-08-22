package mosaic.runtime.internal;

import java.util.Arrays;
import mosaic.runtime.MosaicTriggerIndex;

/** 触发索引实现(M6-B):事件 → 订阅函数 id,直通 C 侧触发表区间扫描
 *  (Native.triggerSubscribers,复用 trigger.c 的 lower_bound,与 dispatch
 *  同一纪律:仅基础 pack,返回按 (event, fn) 排序)。两阶段:探测总长 →
 *  分配精确数组 → 填充;容量不足截断时按实填返回。未注册/无订阅 → 空数组。
 */
public final class TriggerIndexImpl implements MosaicTriggerIndex {
    private final RuntimeImpl rt;

    TriggerIndexImpl(RuntimeImpl rt) { this.rt = rt; }

    /** 事件全部订阅函数 id(排序);未注册事件/无订阅 → 空数组。 */
    public long[] subscribers(int eventId) {
        int n = Native.triggerSubscribers(rt.handle(), eventId, null);
        if (n <= 0) return new long[0];
        long[] out = new long[n];
        int got = Native.triggerSubscribers(rt.handle(), eventId, out);
        return got == n ? out : Arrays.copyOf(out, Math.max(got, 0));
    }
}
