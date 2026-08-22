package mosaic.runtime.internal;

import mosaic.runtime.MosaicTriggerEntry;

/** 触发条目数据类(M6-B):(eventId, fnId) 不可变值对象;由需要处
 *  (TriggerIndexImpl 消费者 / 契约测试)直接构造。 */
public final class TriggerEntryImpl implements MosaicTriggerEntry {
    private final int eventId;
    private final long fnId;

    public TriggerEntryImpl(int eventId, long fnId) {
        this.eventId = eventId;
        this.fnId = fnId;
    }

    public int eventId() { return eventId; }
    public long fnId() { return fnId; }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof TriggerEntryImpl)) return false;
        TriggerEntryImpl t = (TriggerEntryImpl) o;
        return eventId == t.eventId && fnId == t.fnId;
    }

    @Override
    public int hashCode() { return 31 * Integer.hashCode(eventId) + Long.hashCode(fnId); }
}
