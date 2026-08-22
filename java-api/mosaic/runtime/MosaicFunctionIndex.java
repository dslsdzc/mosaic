package mosaic.runtime;

public interface MosaicFunctionIndex {
    /** fnId = moduleId<<32|localId;未命中返回 null。 */
    MosaicFunctionDescriptor find(long fnId);
    long count();
}
