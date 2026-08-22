package mosaic.runtime;

/** 激活策略(可插拔;默认自动:事件驱动物化)。 */
public interface MosaicActivationPolicy {
    boolean shouldMaterialize(long fnId, int eventId);
}
