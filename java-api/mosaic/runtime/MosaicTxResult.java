package mosaic.runtime;

public interface MosaicTxResult {
    boolean ok();
    /** 失败原因(错误信息)。 */
    String error();
}
