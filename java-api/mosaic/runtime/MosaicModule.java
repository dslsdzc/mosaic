package mosaic.runtime;

/** 模块与统一资源归属(ModuleContext 是 Owner)。 */
public interface MosaicModule {
    long moduleId();
    int version();
    int generation();
    String name();
    MosaicModuleContext context();
}
