package mosaic.runtime;

import mosaic.Since;

public interface MosaicEventCatalog {
    /** 目录中全部事件(按名排序);未注册名返回 null。 */
    MosaicEvent find(String name);
    int count();
}
