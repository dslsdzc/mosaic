package mosaic.runtime;

/** 冷态描述符(查询/创造模式浏览零物化)。 */
public interface MosaicFunctionDescriptor {
    long fnId();
    long moduleId();
    int codeOffset();
    int generation();
    int stateSize();
    int costHint();
    int flags();
}
