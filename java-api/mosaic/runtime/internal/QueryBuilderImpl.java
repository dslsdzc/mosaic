package mosaic.runtime.internal;

import mosaic.runtime.MosaicIndexQuery;
import mosaic.runtime.MosaicQuery;
import mosaic.runtime.MosaicQueryBuilder;

/** 查询构建器:byCategory → QueryImpl(经 IndexImpl.items 浏览,零物化)。 */
public final class QueryBuilderImpl implements MosaicQueryBuilder {
    private final MosaicIndexQuery index;

    QueryBuilderImpl(MosaicIndexQuery index) { this.index = index; }

    public MosaicQuery byCategory(int category) {
        return new QueryImpl(index, category);
    }
}
