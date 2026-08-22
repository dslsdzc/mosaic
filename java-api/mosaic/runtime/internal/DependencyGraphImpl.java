package mosaic.runtime.internal;

import java.util.function.LongConsumer;
import mosaic.runtime.MosaicDependencyGraph;

/** 依赖图:直接依赖遍历(单层,非闭包)——depForEach 两阶段(探测总数 → 填充)。
 *  闭包/环检测/版本约束见 dependencyResolver()(M2-1 语义)。 */
public final class DependencyGraphImpl implements MosaicDependencyGraph {
    private final RuntimeImpl rt;

    DependencyGraphImpl(RuntimeImpl rt) { this.rt = rt; }

    public void forEachDep(long moduleId, LongConsumer consumer) {
        if (consumer == null) return;
        int n = Native.depForEach(rt.handle(), moduleId, null);
        if (n <= 0) return;
        long[] out = new long[n];
        int w = Native.depForEach(rt.handle(), moduleId, out);
        for (int i = 0; i < w; i++) consumer.accept(out[i]);
    }
}
