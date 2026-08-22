package mosaic.runtime;

public interface MosaicModuleContext {
    /** 模块拥有的任务/订阅/状态统一处置(module.unload 语义)。 */
    void unload();
    MosaicResourceHandle resource();
}
