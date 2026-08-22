package mosaic.runtime;

/** 函数状态读写与迁移。 */
public interface MosaicFunctionState {
    byte[] read(long fnId);
    /** 写入持久状态槽(墓碑序列化用)。 */
    void write(long fnId, byte[] state);
}
