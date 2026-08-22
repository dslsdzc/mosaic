package mosaic.runtime;

/** 状态迁移钩子(v1_state → v2_state;size = v2 大小)。 */
public interface MosaicStateTransform {
    void transform(byte[] v1State, byte[] v2State, int size);
}
