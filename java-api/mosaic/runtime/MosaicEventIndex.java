package mosaic.runtime;

public interface MosaicEventIndex {
    /** 事件名 → id;未注册 -1。 */
    int id(String name);
    int count();
}
