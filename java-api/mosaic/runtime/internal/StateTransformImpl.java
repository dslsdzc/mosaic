package mosaic.runtime.internal;

import mosaic.runtime.MosaicStateTransform;

/** 状态迁移钩子实现(M6-B):noop 常量 + 静态工厂。语义 = v1_state → v2_state
 *  (size = v2 大小):noop 复制 min(v1 长度, size) 字节前缀并零填充尾部——
 *  "无迁移"的确定性空操作;自定义 transform(如 bytes[0]+=1)由调用方以
 *  λ/匿名类提供,纯 Java 语义,在本类之外直接作用于两段缓冲。
 */
public final class StateTransformImpl implements MosaicStateTransform {

    /** 空操作迁移:复制 v1 前缀,尾部零填充(size 上限;v2 容量不足截断)。 */
    private static final StateTransformImpl NOOP = new StateTransformImpl();

    private StateTransformImpl() {}

    /** 空操作迁移钩子(迁移时原样保留 v1 前缀;打包默认 transform = noop)。 */
    public static MosaicStateTransform noop() { return NOOP; }

    /** noop 语义:复制 v1 前缀,尾部零填充(size 上限)。 */
    public void transform(byte[] v1State, byte[] v2State, int size) {
        if (v1State == null || v2State == null || size < 0) return;
        int n = Math.min(Math.min(v1State.length, size), v2State.length);
        System.arraycopy(v1State, 0, v2State, 0, n);
        if (n < v2State.length && n < size)
            java.util.Arrays.fill(v2State, n, Math.min(v2State.length, size), (byte) 0);
    }
}
