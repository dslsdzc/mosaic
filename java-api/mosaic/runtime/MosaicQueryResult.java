package mosaic.runtime;

public interface MosaicQueryResult {
    long count();
    /** 当前页描述符(零物化)。 */
    mosaic.runtime.MosaicItemDescriptor get(int index);
}
