package mosaic.runtime;

/** 索引查询(纯冷态,零物化)。 */
public interface MosaicIndexQuery {
    MosaicFunctionIndex functions();
    MosaicModuleIndex modules();
    MosaicEventIndex events();
    MosaicItemIndex items();
}
