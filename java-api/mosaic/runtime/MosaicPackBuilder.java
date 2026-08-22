package mosaic.runtime;

import mosaic.Since;

/** pack 构建器(离线工具;JNI 逐记录调用 C builder)。 */
public interface MosaicPackBuilder {
    static MosaicPackBuilder create(String path, long moduleCount, long fnCount,
                                    long triggerCount, long depCount, int eventCount) {
        return mosaic.runtime.internal.PackBuilderImpl.create(path, moduleCount, fnCount,
                triggerCount, depCount, eventCount);
    }
    void addEvent(String name);
    void addModule(long moduleId, int version, String name, String soPath);
    void addFn(long moduleId, long localId, int codeOff, int stateSize, int generation,
               int costHint, int flags);
    void setFnTransform(long fnId, int transformIndex);
    void addTrigger(int eventId, long fnId);
    void addDep(long ownerId, long depId);
    void setItemCount(long itemCount);
    void addItem(long providerFnId, String name, String tags, int category, String iconRef, int flags);
    /** 排序/校验/写出;失败返回 -1(错误信息经 lastError)。 */
    int finish();
}
