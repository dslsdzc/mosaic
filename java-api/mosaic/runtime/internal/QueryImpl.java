package mosaic.runtime.internal;

import java.util.ArrayList;
import java.util.List;
import mosaic.runtime.MosaicIndexQuery;
import mosaic.runtime.MosaicItemDescriptor;
import mosaic.runtime.MosaicQuery;
import mosaic.runtime.MosaicQueryResult;

/** 查询实现:items(category) 经 item 索引枚举收集为结果页(零物化)。 */
public final class QueryImpl implements MosaicQuery {
    private final MosaicIndexQuery index;
    private final int category;

    QueryImpl(MosaicIndexQuery index, int category) {
        this.index = index;
        this.category = category;
    }

    public MosaicQueryResult items(int category) {
        List<MosaicItemDescriptor> list = new ArrayList<>();
        index.items().forEach(category, list::add);
        return new QueryResultImpl(list);
    }

    static final class QueryResultImpl implements MosaicQueryResult {
        private final List<MosaicItemDescriptor> items;
        QueryResultImpl(List<MosaicItemDescriptor> items) { this.items = items; }
        public long count() { return items.size(); }
        public MosaicItemDescriptor get(int index) { return items.get(index); }
    }
}
