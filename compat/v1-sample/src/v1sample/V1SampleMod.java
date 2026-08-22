package v1sample;

import mosaic.MosaicApi;
import mosaic.runtime.*;

/** v1 API 兼容样例:只使用 API_VERSION 1 引入的成员;编译成功是只增不减的第一道门。 */
public final class V1SampleMod {
    public static void main(String[] args) throws Exception {
        MosaicApi.requireApi(1);
        if (args.length < 1) { System.err.println("usage: V1SampleMod <pack>"); System.exit(2); }
        MosaicRuntime rt = MosaicRuntime.open(new String[]{args[0]});
        long n = rt.functionCount();
        int join = rt.eventId("player_join");
        int executed = rt.eventDispatch(join, new byte[4]);
        MosaicFunctionLifecycle lc = rt.lifecycle();
        long h = lc.materialize(0x100000000L);
        byte[] st = lc.state(h);
        System.out.println("V1 SAMPLE OK: functions=" + n + " dispatch=" + executed
                + " state0=" + (st != null ? java.nio.ByteBuffer.wrap(st).getInt(0) : -1));
        rt.close();
    }
}
