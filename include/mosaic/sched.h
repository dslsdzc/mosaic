#ifndef MOSAIC_SCHED_H
#define MOSAIC_SCHED_H
/* M2-3:DAG 任务调度器(线程池 + 依赖图执行)
 *
 * 设计规格第 22 节(M2 调度器):task 依赖/读-写集/优先级/亲和性/取消/checkpoint;
 * "并行化的是实际工作,不是盲目为每 Mod 建线程"。本库交付独立的 DAG 调度器,
 * 供启动并行化与并行物化使用(调用方把"实际工作"切成任务提交,调度器负责
 * 依赖排序 + 并发执行)。
 *
 * 零外部依赖:仅 libc + pthread。
 * 线程安全:所有 API 可并发调用(内部互斥)。
 */

#include "mosaic/base.h"

#define MOSAIC_SCHED_MAX_DEPS 8

typedef struct mosaic_task mosaic_task;
typedef void (*mosaic_task_fn)(void *arg);
/* 任务暂停/取消时的状态钩子。语义见 mosaic_sched_cancel 注释。 */
typedef void (*mosaic_checkpoint_fn)(mosaic_task *t, void *ctx);

typedef struct {
  u64 id;                                /* 唯一 id(调用方分配,重复 → submit 失败) */
  u32 dep_ids[MOSAIC_SCHED_MAX_DEPS];    /* 依赖任务 id;dep_count == 0 = 无依赖 */
  u32 dep_count;                         /* > MOSAIC_SCHED_MAX_DEPS → submit 失败 */
  int priority;                          /* 高 = 先执行(同就绪集内) */
  int affinity;                          /* -1 = 任意 worker;>=0 = 固定 worker 下标(越界 → submit 失败) */
  mosaic_task_fn fn;                     /* 实际工作;调度器仅负责在依赖满足后于某 worker 线程调用 */
  void *arg;
  mosaic_checkpoint_fn checkpoint;       /* 可选:运行中被取消时,fn 返回后由调度器调用以保存状态 */
  void *checkpoint_ctx;
} mosaic_task_spec;

typedef struct mosaic_sched mosaic_sched;

/* 创建 n_workers 个 worker 线程的调度器(n_workers < 1 → NULL)。 */
mosaic_sched *mosaic_sched_create(int n_workers);

/* 等待全部任务完成/取消后销毁(内部先 wait_all,再停 worker 线程)。
   销毁后不得再使用 s;调用方不得保留任务指针(mosaic_task 为不透明句柄,
   仅供 checkpoint 回调传递,不提供任何访问器,也不应在 wait_all/destroy
   之后被解引用)。 */
void mosaic_sched_destroy(mosaic_sched *s);

/* 提交一个任务。0 = 成功;-1 = 参数错误(ffn 为空、dep_count 越界、affinity 越界、
   依赖任务 id 未知(必须全部已提交)、task id 重复)。任务立即进入调度,无需额外 kick。
   依赖中已完成的跳过等待;依赖中已被取消的 → 本任务直接以"已取消"收尾。 */
int mosaic_sched_submit(mosaic_sched *s, const mosaic_task_spec *spec);

/* 阻塞至全部已提交任务完成/取消。返回以"已取消"收尾的任务数(显式取消 +
   级联取消;正常完成不计)。所有任务最终都到达终态(取消会级联传播,见下),
   仅当 fn 自身不返回时才会永久阻塞(用户代码问题,非调度器缺陷)。 */
int mosaic_sched_wait_all(mosaic_sched *s);

/* 取消一个任务。
 * - 未开始任务(pending / 就绪 / 已在 worker 队列) :立即取消,fn 不被执行,
 *   不调用 checkpoint,返回 0。
 * - 运行中任务:不可抢占——fn 继续执行到返回;返回 0,任务完成时被标记为
 *   "已取消",且若提供了 checkpoint,调度器在 fn 返回后、完成计数之前调用
 *   checkpoint(t, checkpoint_ctx)(worker 线程内)。因此 wait_all 返回时全部
 *   checkpoint 必然已执行完毕。checkpoint 是任务得知自己被取消的通道(如把
 *   checkpoint_ctx 指向的共享标志位/状态落地);fn 本身无法访问调度器,如需
 *   协作应定期读 checkpoint_ctx 中的共享状态。
 *   注意:fn 与 checkpoint 在 worker 线程内执行,内部不得调用 wait_all 或
 *   destroy(自死锁);cancel 与 submit 可安全调用。
 * - 未知 id 或已终态(done/cancelled):-1。
 * 取消传播:被取消任务的每个依赖者都会级联取消(其依赖永远无法满足),级联
 * 取消不回调 checkpoint;级联链上的所有任务计入 wait_all 的返回值。 */
int mosaic_sched_cancel(mosaic_sched *s, u64 task_id);

/* 未完成任务数(pending + 就绪 + 已领走 + 运行中;不含已取消/已完成)。 */
u32 mosaic_sched_pending(mosaic_sched *s);

#endif /* MOSAIC_SCHED_H */
