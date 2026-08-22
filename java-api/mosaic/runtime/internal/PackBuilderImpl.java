package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicPackBuilder;

/** pack 构建器实现:逐记录直通 C builder(JNI);Cleaner 兜底 packFree 防泄漏
 *  (GC 回收未 finish/未 free 的 builder 句柄)。 */
public final class PackBuilderImpl implements MosaicPackBuilder {
    private static final java.lang.ref.Cleaner CLEANER = java.lang.ref.Cleaner.create();

    private final State state;

    private PackBuilderImpl(long b) {
        this.state = new State(b);
        CLEANER.register(this, state);
    }

    public static MosaicPackBuilder create(String path, long moduleCount, long fnCount,
                                           long triggerCount, long depCount, int eventCount) {
        long h = Native.packCreate(path, moduleCount, fnCount, triggerCount, depCount, eventCount);
        if (h == 0) throw new MosaicHandleException("pack builder create failed");
        return new PackBuilderImpl(h);
    }

    public void addEvent(String name) { Native.packAddEvent(state.b, name); }
    public void addModule(long moduleId, int version, String name, String soPath) {
        Native.packAddModule(state.b, moduleId, version, name, soPath);
    }
    public void addFn(long moduleId, long localId, int codeOff, int stateSize, int generation,
                      int costHint, int flags) {
        Native.packAddFn(state.b, moduleId, localId, codeOff, stateSize, generation, costHint, flags);
    }
    public void setFnTransform(long fnId, int transformIndex) {
        Native.packSetFnTransform(state.b, fnId, transformIndex);
    }
    public void addTrigger(int eventId, long fnId) { Native.packAddTrigger(state.b, eventId, fnId); }
    public void addDep(long ownerId, long depId) { Native.packAddDep(state.b, ownerId, depId); }
    public void setItemCount(long itemCount) { Native.packSetItemCount(state.b, itemCount); }
    public void addItem(long providerFnId, String name, String tags, int category, String iconRef, int flags) {
        Native.packAddItem(state.b, providerFnId, name, tags, category, iconRef, flags);
    }
    public int finish() { return Native.packFinish(state.b); }

    /** 句柄独占的 Cleaner 状态:回收时释放 C builder(finish 后 packFree 幂等)。 */
    static final class State implements Runnable {
        private long b;
        State(long b) { this.b = b; }
        public void run() {
            if (b != 0) { Native.packFree(b); b = 0; }
        }
    }
}
