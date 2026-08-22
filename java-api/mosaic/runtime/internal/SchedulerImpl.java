package mosaic.runtime.internal;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import mosaic.runtime.MosaicCheckpoint;
import mosaic.runtime.MosaicScheduler;
import mosaic.runtime.MosaicTask;

/** DAG 调度器实现(纯 Java:线程池 + 依赖计数;C 侧 sched 留作内核内部)。
 *  语义对齐 mosaic_sched_*(sched.h):submit 校验(重复 id / 未知依赖 /
 *  MAX_DEPS=8 越界)、依赖满足即派发、取消(未开始立即取消 + 级联;运行中
 *  不可抢占,fn 返回后调用 checkpoint)、waitAll 返回取消数。 */
public final class SchedulerImpl implements MosaicScheduler {
    static final int MAX_DEPS = 8;

    private final ExecutorService pool = Executors.newCachedThreadPool();
    private final Object lock = new Object();
    private final Map<Long, TaskEntry> tasks = new HashMap<>();
    private int cancelledCount = 0;

    public int submit(MosaicTask task) {
        if (task == null) return -1;
        int[] deps = task.dependencyIds();
        if (deps != null && deps.length > MAX_DEPS) return -1;
        TaskEntry e = new TaskEntry(task, deps);
        synchronized (lock) {
            if (tasks.containsKey(task.id())) return -1;
            boolean cancelled = false;
            for (int d : deps) {
                TaskEntry dep = tasks.get((long) d);
                if (dep == null || !dep.submitted) return -1;   /* 依赖必须已提交 */
                if (dep.cancelled) { cancelled = true; break; }
                if (!dep.done) { e.remaining++; dep.dependents.add(e); }
            }
            tasks.put(task.id(), e);
            e.submitted = true;
            if (cancelled) { e.cancelled = true; e.done = true; cancelledCount++; }
            else if (e.remaining == 0) runNow(e);
            lock.notifyAll();
            return 0;
        }
    }

    public int waitAll() {
        synchronized (lock) {
            while (true) {
                boolean any = false;
                for (TaskEntry e : tasks.values())
                    if (!e.done) { any = true; break; }
                if (!any) break;
                try { lock.wait(); }
                catch (InterruptedException ie) { Thread.currentThread().interrupt(); return -1; }
            }
            int cancelled = cancelledCount;
            cancelledCount = 0;   /* 每次 waitAll 返回该轮累计取消数(对齐 C) */
            return cancelled;
        }
    }

    public int cancel(long taskId) {
        synchronized (lock) {
            TaskEntry e = tasks.get(taskId);
            if (e == null || e.done || e.cancelled) return -1;
            e.cancelled = true;
            if (!e.running) {
                e.done = true;               /* 未开始:立即取消,fn 不执行 */
                cancelledCount++;
                cascadeCancel(e);
                lock.notifyAll();
            }
            /* 运行中:不可抢占——fn 返回后由 finish 调 checkpoint + 级联 */
            return 0;
        }
    }

    public int pendingCount() {
        synchronized (lock) {
            int n = 0;
            for (TaskEntry e : tasks.values())
                if (!e.done && !e.cancelled) n++;
            return n;
        }
    }

    /* 依赖满足 → 派发到 worker */
    private void runNow(TaskEntry e) {
        e.running = true;
        pool.execute(() -> {
            if (!e.cancelled) {
                try { e.task.run(); }
                catch (Throwable t) { /* 任务异常不破坏调度;结果语义由任务自持 */ }
            }
            finish(e);
        });
    }

    private void finish(TaskEntry e) {
        MosaicCheckpoint cp = null;
        synchronized (lock) {
            e.running = false;
            e.done = true;
            if (e.cancelled) {
                cancelledCount++;
                if (e.task.checkpoint() != null) cp = e.task.checkpoint();
            }
            for (TaskEntry dep : e.dependents) {
                if (dep.cancelled || dep.done) continue;
                if (e.cancelled) {            /* 依赖无法完成 → 级联取消 */
                    dep.cancelled = true; dep.done = true; cancelledCount++;
                    cascadeCancel(dep);
                } else if (--dep.remaining == 0) {
                    runNow(dep);
                }
            }
            e.dependents.clear();
            lock.notifyAll();
        }
        if (cp != null) {
            try { cp.save(e.task); }          /* 运行中被取消:fn 返回后由调度器调用 */
            catch (Throwable t) { }
        }
    }

    /* 级联取消(不回调 checkpoint;与 C sched.h 语义一致)。调用方持锁。 */
    private void cascadeCancel(TaskEntry e) {
        for (TaskEntry dep : e.dependents) {
            if (dep.done || dep.cancelled) continue;
            dep.cancelled = true; dep.done = true; cancelledCount++;
            cascadeCancel(dep);
        }
        e.dependents.clear();
    }

    static final class TaskEntry {
        final MosaicTask task;
        final int[] deps;
        int remaining;
        boolean submitted, done, cancelled, running;
        final List<TaskEntry> dependents = new ArrayList<>();
        TaskEntry(MosaicTask task, int[] deps) {
            this.task = task;
            this.deps = deps == null ? new int[0] : deps.clone();
        }
    }
}
