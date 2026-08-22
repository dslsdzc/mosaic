package mosaic.runtime;

public interface MosaicDependencyResolver {
    /** 依赖闭包(含自身,拓扑序,依赖先于依赖者);失败抛 MosaicApiException。 */
    long[] resolve(long moduleId, MosaicVersionConstraint self);
}
