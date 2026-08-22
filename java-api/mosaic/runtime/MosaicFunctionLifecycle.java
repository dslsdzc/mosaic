package mosaic.runtime;

import mosaic.MosaicHandleException;

/** 函数生命周期:物化/执行/墓碑/状态(函数级惰性的 API 面)。 */
public interface MosaicFunctionLifecycle {
    /** 物化(COLD→ACTIVE;TOMBSTONED→恢复);返回句柄;失败抛 MosaicHandleException。 */
    long materialize(long fnId);
    /** 热路径执行(句柄必须有效;载荷小端)。 */
    void execute(long fnHandle, int eventId, byte[] payload);
    /** 墓碑(refs==0 才可);返回 0 成功、-1 失败(lastError 取因)。 */
    int tombstone(long fnHandle);
    /** 读取函数状态(64B 上限);未物化返回 null。 */
    byte[] state(long fnHandle);
    /** 句柄 → fnId。 */
    long fnIdOf(long fnHandle);
}
