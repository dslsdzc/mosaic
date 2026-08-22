package mosaic;

/** API 异常基类。 */
public class MosaicApiException extends RuntimeException {
    public MosaicApiException(String msg) { super(msg); }
    public MosaicApiException(String msg, Throwable cause) { super(msg, cause); }
}
