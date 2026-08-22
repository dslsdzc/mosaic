package mosaic.vanilla;

import mosaic.Since;

public interface MosaicBlockState {
    MosaicBlock block();
    /** 属性集(如 lit/waterlogged);两代共同语义。 */
    String[] propertyNames();
    /** 属性值(字符串形态);未知属性抛 MosaicHandleException。 */
    String property(String name);
}
