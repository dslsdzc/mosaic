package mosaic.runtime.internal;

import mosaic.runtime.MosaicOwnedResource;

/** 所有权包装:dispose 一次性回调(幂等——二次 dispose 无操作)。
 *  of(disposer) 工厂;disposer 为空抛 NullPointerException。 */
public final class OwnedResourceImpl implements MosaicOwnedResource {
    private final Runnable disposer;
    private boolean disposed;

    private OwnedResourceImpl(Runnable disposer) { this.disposer = disposer; }

    public static MosaicOwnedResource of(Runnable disposer) {
        if (disposer == null) throw new NullPointerException("disposer");
        return new OwnedResourceImpl(disposer);
    }

    public void dispose() {
        if (disposed) return;
        disposed = true;
        disposer.run();
    }
}
