import mosaic.MosaicApi;
import mosaic.MosaicApiVersionException;

public class ApiVersionTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }
    public static void main(String[] args) {
        check(MosaicApi.API_VERSION == 1, "API_VERSION==1");
        MosaicApi.requireApi(1);                       // 不抛
        boolean threw = false;
        try { MosaicApi.requireApi(2); }               // 声明 [1,2] > 1 → 拒绝
        catch (MosaicApiVersionException e) { threw = true; }
        check(threw, "requireApi(2) rejected");
        check(MosaicApi.API_VERSION >= 1, "API_VERSION monotonic");
        if (failures == 0) System.out.println("API VERSION TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
