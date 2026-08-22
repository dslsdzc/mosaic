package mosaic.runtime;

/** DAG 调度器(线程池 + 依赖图执行)。 */
public interface MosaicScheduler {
    int submit(MosaicTask task);
    int waitAll();
    int cancel(long taskId);
    int pendingCount();
}
