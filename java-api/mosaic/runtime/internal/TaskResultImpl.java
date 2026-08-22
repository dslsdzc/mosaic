package mosaic.runtime.internal;

import mosaic.runtime.MosaicTaskResult;

/** 任务结果数据类(纯 Java):ok / error 二态。 */
public final class TaskResultImpl implements MosaicTaskResult {
    private final boolean ok;
    private final String error;

    private TaskResultImpl(boolean ok, String error) {
        this.ok = ok;
        this.error = error;
    }

    public static MosaicTaskResult success() { return new TaskResultImpl(true, null); }
    public static MosaicTaskResult failed(String error) { return new TaskResultImpl(false, error); }

    public boolean ok() { return ok; }
    public String error() { return error; }
}
