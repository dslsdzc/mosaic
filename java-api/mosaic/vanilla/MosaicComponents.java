package mosaic.vanilla;

import mosaic.Since;

public interface MosaicComponents {
    /** 组件键集合;26.2 DataComponent 映射,1.8.9 返回空。 */
    String[] keys();
    /** 组件字节(序列化形态);未知键返回 null。 */
    byte[] get(String key);
    @Since(1)
    MosaicComponents with(String key, byte[] value);
}
