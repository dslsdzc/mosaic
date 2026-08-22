package mosaic;

/** mod 声明所需 API 版本 > 运行时 API_VERSION。 */
public class MosaicApiVersionException extends MosaicApiException {
    public MosaicApiVersionException(String msg) { super(msg); }
}
