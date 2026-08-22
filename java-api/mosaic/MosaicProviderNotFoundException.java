package mosaic;

/** 当前 MC 版本无匹配 Provider。 */
public class MosaicProviderNotFoundException extends MosaicApiException {
    public MosaicProviderNotFoundException(String msg) { super(msg); }
}
