package mosaic.runtime.internal;

import mosaic.runtime.MosaicModuleInfo;

/** 模块信息(冷态,modDescField/modDescString 包装):
 *  moduleId/version/soPath/fnCount(fnCount = 模块记录内计数,modDescField 字段 3)。 */
public final class ModuleInfoImpl implements MosaicModuleInfo {
    private final long moduleId;
    private final int version;
    private final String soPath;
    private final int fnCount;

    ModuleInfoImpl(long moduleId, int version, String soPath, int fnCount) {
        this.moduleId = moduleId;
        this.version = version;
        this.soPath = soPath;
        this.fnCount = fnCount;
    }

    public long moduleId() { return moduleId; }
    public int version() { return version; }
    public String soPath() { return soPath; }
    public int fnCount() { return fnCount; }
}
