package mosaic.runtime;

/** 查询(创造模式:浏览描述符不物化)。 */
public interface MosaicQuery {
    /** 按分类浏览全部 item 描述符。 */
    MosaicQueryResult items(int category);
}
