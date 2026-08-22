package mosaic.runtime;

/** 触发索引(事件 → 订阅函数区间)。 */
public interface MosaicTriggerIndex {
    /** 事件全部订阅函数 id(排序)。 */
    long[] subscribers(int eventId);
}
