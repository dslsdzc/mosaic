package mosaic.runtime.internal;

import mosaic.MosaicApiException;
import mosaic.runtime.MosaicDependencyResolver;
import mosaic.runtime.MosaicVersionConstraint;

/** 依赖闭包解析:直通 mosaic_dep_resolve(两阶段:探测长度 → 填充)。 */
public final class DependencyResolverImpl implements MosaicDependencyResolver {
    private final RuntimeImpl rt;

    DependencyResolverImpl(RuntimeImpl rt) { this.rt = rt; }

    public long[] resolve(long moduleId, MosaicVersionConstraint self) {
        int min = self != null ? self.minVersion() : 0;
        int max = self != null ? self.maxVersion() : 0;
        long h = rt.handle();
        int len = Native.depResolve(h, moduleId, min, max, null);
        if (len < 0)
            throw new MosaicApiException("dep resolve failed (lastError=" + Native.lastError(h) + ")");
        long[] out = new long[len];
        int n = Native.depResolve(h, moduleId, min, max, out);
        if (n < 0)
            throw new MosaicApiException("dep resolve failed (lastError=" + Native.lastError(h) + ")");
        return out;
    }
}
