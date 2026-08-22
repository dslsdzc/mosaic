package mosaic;

/** 句柄失效(墓碑/卸载后访问)。 */
public class MosaicHandleException extends MosaicApiException {
    public MosaicHandleException(String msg) { super(msg); }
}
