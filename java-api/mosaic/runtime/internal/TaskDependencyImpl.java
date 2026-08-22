package mosaic.runtime.internal;

import mosaic.runtime.MosaicTaskDependency;

/** 任务依赖数据类(纯 Java):taskId。 */
public final class TaskDependencyImpl implements MosaicTaskDependency {
    private final long taskId;

    public TaskDependencyImpl(long taskId) { this.taskId = taskId; }

    public long taskId() { return taskId; }
}
