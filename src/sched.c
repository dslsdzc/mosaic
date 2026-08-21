/* src/sched.c — M2-3:DAG 任务调度器(线程池 + 依赖图执行)
 *
 * 结构:
 *  - n_workers 个 worker 线程,每 worker 一条互斥队列(锁序:全局锁 → worker 锁;
 *    worker 对别的队列只 trylock,绝不阻塞 → 无死锁);
 *  - 任务状态机:PENDING(依赖未满)→ READY(就绪集)→ CLAIMED(已领走,worker
 *    队列)→ RUNNING(执行中)→ DONE / CANCELLED;PENDING 的级联取消直达 CANCELLED;
 *  - 就绪集 = 按 (priority desc, submit_seq asc) 排序的侵入式链表;worker 空闲时
 *    取自己可领的最高优先级任务(affinity == -1 或 == 本 worker);
 *  - 无就绪任务时 worker 用 trylock 偷其他 worker 队列的**队尾**(未开始任务;
 *    affinity 固定的任务仅目标 worker 可偷);
 *  - 依赖完成传播:任务终态后对每个依赖者递减 deps_remaining,归零入就绪集;
 *    依赖被取消 → 依赖者级联取消(全部在全局锁内,原子);
 *  - 取消:未开始任务从就绪集/worker 队列摘除(无 checkpoint);运行中任务置取消
 *    标志,fn 返回后由调度器调用 checkpoint,再标 CANCELLED(见 sched.h 语义);
 *  - 错误处理:submit 未知依赖/重复 id/参数越界 → -1;cancel 未知/已终态 → -1;
 *    fn 崩溃 → 未定义(与 M1 风格一致,用户代码问题,文档注明)。
 *
 * 零外部依赖:仅 libc + pthread。全部 API 线程安全。
 */
#include "mosaic/sched.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

enum {
  TASK_PENDING = 0,   /* 已提交,依赖未全部完成 */
  TASK_READY,         /* 依赖全完成,在就绪集 */
  TASK_CLAIMED,       /* 已被 worker 领走,在 worker 队列,未开始 */
  TASK_RUNNING,       /* fn 执行中 */
  TASK_DONE,          /* fn 正常返回 */
  TASK_CANCELLED,     /* 取消(显式/级联/运行中被标记) */
};

struct mosaic_task {
  /* spec 字段(提交时拷贝) */
  u64 id;
  u32 dep_ids[MOSAIC_SCHED_MAX_DEPS];
  u32 dep_count;
  int priority;
  int affinity;
  mosaic_task_fn fn;
  void *arg;
  mosaic_checkpoint_fn checkpoint;
  void *checkpoint_ctx;
  /* 调度字段(全局锁保护) */
  int state;
  int cancelled;          /* 运行中被请求取消 */
  int cancelled_by_dep;   /* 有依赖被取消 → 永不可执行 */
  u32 deps_remaining;
  u64 submit_seq;
  int owner;              /* 领走它的 worker 下标(-1 = 未领走) */
  struct mosaic_task *ready_next;
  struct mosaic_task *queue_next;   /* worker 队列链 */
  struct mosaic_task *hash_next;    /* 哈希链 */
  struct mosaic_task **dependents;  /* 依赖本任务的任务表(提交时挂边) */
  u32 n_dependents;
};

typedef struct {
  pthread_t thread;
  pthread_mutex_t lock;
  struct mosaic_sched *sched;
  struct mosaic_task *q_head, *q_tail;
} sched_worker;

struct mosaic_sched {
  pthread_mutex_t lock;
  pthread_cond_t cv;
  int n_workers;
  sched_worker *workers;
  int stop;
  /* 任务表:链式哈希(id → task) */
  struct mosaic_task **buckets;
  u32 nbuckets;
  u32 task_count;
  /* 就绪集(按 priority desc, submit_seq asc) */
  struct mosaic_task *ready_head;
  /* 计数 */
  u64 submitted;
  u64 completed;
  u64 cancelled_total;
  u64 submit_seq;
};

/* ---- 哈希表 ---- */
static u32 hash_id(u64 id, u32 nbuckets) {
  return (u32)((id * 0x9E3779B97F4A7C15ull) & (nbuckets - 1));
}

static mosaic_task *table_find(mosaic_sched *s, u64 id) {
  mosaic_task *t = s->buckets[hash_id(id, s->nbuckets)];
  while (t && t->id != id) t = t->hash_next;
  return t;
}

static void table_grow(mosaic_sched *s) {
  u32 nn = s->nbuckets * 2;
  struct mosaic_task **nb = calloc(nn, sizeof *nb);
  if (!nb) return;   /* 空间不足不增长,线性退化可接受 */
  for (u32 i = 0; i < s->nbuckets; i++) {
    mosaic_task *t = s->buckets[i];
    while (t) {
      mosaic_task *nx = t->hash_next;
      u32 h = hash_id(t->id, nn);
      t->hash_next = nb[h];
      nb[h] = t;
      t = nx;
    }
  }
  free(s->buckets);
  s->buckets = nb;
  s->nbuckets = nn;
}

static void table_insert(mosaic_sched *s, mosaic_task *t) {
  if (s->task_count + 1 > s->nbuckets * 3 / 4) table_grow(s);
  u32 h = hash_id(t->id, s->nbuckets);
  t->hash_next = s->buckets[h];
  s->buckets[h] = t;
  s->task_count++;
}

/* ---- 就绪集(有序链表) ---- */
static void ready_insert(mosaic_sched *s, mosaic_task *t) {
  mosaic_task **pp = &s->ready_head;
  while (*pp) {
    mosaic_task *cur = *pp;
    if (cur->priority < t->priority ||
        (cur->priority == t->priority && cur->submit_seq > t->submit_seq))
      break;
    pp = &cur->ready_next;
  }
  t->ready_next = *pp;
  *pp = t;
}

static void ready_remove(mosaic_sched *s, mosaic_task *t) {
  mosaic_task **pp = &s->ready_head;
  while (*pp && *pp != t) pp = &(*pp)->ready_next;
  if (*pp) *pp = t->ready_next;
}

/* 取本 worker 可领的最高优先级任务(affinity 兼容),已持全局锁 */
static mosaic_task *claim_best(mosaic_sched *s, int widx) {
  mosaic_task **pp = &s->ready_head;
  while (*pp) {
    mosaic_task *t = *pp;
    if (t->affinity == -1 || t->affinity == widx) {
      *pp = t->ready_next;
      t->state = TASK_CLAIMED;
      t->owner = widx;
      return t;
    }
    pp = &t->ready_next;
  }
  return NULL;
}

/* ---- worker 队列(锁序:全局 → worker;偷取仅 trylock,不构成死锁) ---- */
static void wq_push(sched_worker *w, mosaic_task *t) {
  pthread_mutex_lock(&w->lock);
  t->queue_next = NULL;
  if (w->q_tail) w->q_tail->queue_next = t;
  else w->q_head = t;
  w->q_tail = t;
  pthread_mutex_unlock(&w->lock);
}

static mosaic_task *wq_pop(sched_worker *w) {
  pthread_mutex_lock(&w->lock);
  mosaic_task *t = w->q_head;
  if (t) {
    w->q_head = t->queue_next;
    if (!w->q_head) w->q_tail = NULL;
  }
  pthread_mutex_unlock(&w->lock);
  return t;
}

static void wq_remove(sched_worker *w, mosaic_task *t) {
  pthread_mutex_lock(&w->lock);
  mosaic_task **pp = &w->q_head;
  mosaic_task *prev = NULL;   /* 扫描中 t 的前驱(摘队尾时重定位 q_tail 用) */
  while (*pp && *pp != t) { prev = *pp; pp = &(*pp)->queue_next; }
  if (*pp) {
    *pp = t->queue_next;
    if (w->q_tail == t) w->q_tail = prev;   /* M2 遗留修复:摘除队尾后 q_tail
       必须重定位到新的队尾(链表前驱;NULL = 队列已空)——旧代码置 NULL 而
       队列非空,违反 "q_tail==NULL ⟺ 空" 不变式,后续 wq_push 走 else 分支
       覆盖 q_head,整条队列入队任务丢失 */
  }
  pthread_mutex_unlock(&w->lock);
}

/* 偷其他 worker 队列的队尾(未开始任务)。affinity 固定的任务仅目标 worker 可偷。
   已持全局锁;trylock 失败即跳过,绝不阻塞。 */
static mosaic_task *steal_any(mosaic_sched *s, int widx) {
  for (int k = 1; k < s->n_workers; k++) {
    int i = (widx + k) % s->n_workers;
    sched_worker *w = &s->workers[i];
    if (pthread_mutex_trylock(&w->lock) != 0) continue;
    mosaic_task *t = w->q_tail;
    if (t && t->affinity != -1 && t->affinity != widx) t = NULL;   /* 不可偷,留下 */
    if (t) {
      /* 单链表摘队尾:从头找前驱 */
      if (w->q_head == t) {
        w->q_head = NULL;
        w->q_tail = NULL;
      } else {
        mosaic_task *p = w->q_head;
        while (p->queue_next != t) p = p->queue_next;
        p->queue_next = NULL;
        w->q_tail = p;
      }
      t->state = TASK_RUNNING;
      t->owner = widx;
    }
    pthread_mutex_unlock(&w->lock);
    if (t) return t;
  }
  return NULL;
}

/* ---- 终态传播:对每个依赖者递减计数;取消级联;归零入就绪集(已持全局锁) ---- */
static void finish_notify(mosaic_sched *s, mosaic_task *x, int x_cancelled) {
  for (u32 i = 0; i < x->n_dependents; i++) {
    mosaic_task *y = x->dependents[i];
    if (y->state == TASK_DONE || y->state == TASK_CANCELLED) continue; /* 已终态 */
    y->deps_remaining--;
    if (x_cancelled) y->cancelled_by_dep = 1;
    if (y->cancelled_by_dep || y->cancelled) {
      /* 依赖被取消(或自身已被标记):级联取消,不可执行 */
      y->state = TASK_CANCELLED;
      s->cancelled_total++;
      s->completed++;
      finish_notify(s, y, 1);
    } else if (y->deps_remaining == 0) {
      y->state = TASK_READY;
      ready_insert(s, y);
    }
  }
}

/* 取消的公共主体(已持全局锁):把未开始任务置终态并传播 */
static void cancel_pending_locked(mosaic_sched *s, mosaic_task *t) {
  if (t->state == TASK_READY) ready_remove(s, t);
  else if (t->state == TASK_CLAIMED) wq_remove(&s->workers[t->owner], t);
  t->state = TASK_CANCELLED;
  s->cancelled_total++;
  s->completed++;
  finish_notify(s, t, 1);
}

/* 批量领走上限:一程领一叠入本 worker 队列,留出可偷空间(steal 目标) */
#define CLAIM_BATCH 8

/* ---- worker 主循环 ---- */
static void run_task(mosaic_sched *s, mosaic_task *t) {
  t->fn(t->arg);   /* 用户代码,无任何锁 */

  /* checkpoint 在完成计数前调用,保证 wait_all 返回时 checkpoint 已全部执行完 */
  pthread_mutex_lock(&s->lock);
  int was_cancelled = t->cancelled;
  pthread_mutex_unlock(&s->lock);
  if (was_cancelled && t->checkpoint) t->checkpoint(t, t->checkpoint_ctx);

  /* M2 遗留修复(TOCTOU):落终态时(持锁)**重读** t->cancelled——fn 返回后到
     本处之间 cancel 随时可能到达(窗口含 checkpoint 时长),若仍按首次读取的
     was_cancelled 落 DONE,与 cancel() 契约不符(运行中取消的任务必须以
     CANCELLED 收尾并计入 cancelled_total,wait_all 返回取消数才正确)。
     重读置位时按 CANCELLED 落终态并计数(checkpoint 未调用可接受:取消落在
     checkpoint 决策之后,checkpoint 是尽力而为)。 */
  pthread_mutex_lock(&s->lock);
  int cancelled = t->cancelled;
  if (cancelled) {
    t->state = TASK_CANCELLED;
    s->cancelled_total++;
  } else {
    t->state = TASK_DONE;
  }
  s->completed++;
  finish_notify(s, t, cancelled);
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->lock);
}

static void *worker_main(void *arg) {
  sched_worker *w = arg;
  mosaic_sched *s = w->sched;
  int widx = (int)(w - s->workers);
  for (;;) {
    /* 1. 本 worker 队列(摘链只锁本 worker;运行任务时全局锁不持,队列即
          "running 队列",队尾可被其他 worker 偷) */
    mosaic_task *t = wq_pop(w);
    if (t) {
      /* CLAIMED → RUNNING 在全局锁内转(与取消的 CLAIMED→CANCELLED 竞争:
         谁先拿全局锁谁赢;cancel 先 → 摘链后已标记 CANCELLED → 跳过执行,
         fn 不运行;pop 先 → 取消走"运行中"路径) */
      pthread_mutex_lock(&s->lock);
      if (t->state == TASK_CLAIMED) t->state = TASK_RUNNING;
      int skip = (t->state != TASK_RUNNING);
      pthread_mutex_unlock(&s->lock);
      if (skip) continue;
      run_task(s, t);
      continue;
    }
    /* 2. 领就绪任务(批量,优先序;affinity 仅目标 worker 可领);无可领则
          偷其他 worker 队列队尾;再无可做 → 等。检查与 cond_wait 在同一
          锁内原子完成:广播只可能唤醒,不会丢失(无 lost-wakeup)。 */
    pthread_mutex_lock(&s->lock);
    if (s->stop) { pthread_mutex_unlock(&s->lock); break; }
    t = claim_best(s, widx);
    if (t) {
      wq_push(w, t);
      for (int k = 1; k < CLAIM_BATCH; k++) {
        mosaic_task *u = claim_best(s, widx);
        if (!u) break;
        wq_push(w, u);
      }
      pthread_mutex_unlock(&s->lock);
      continue;
    }
    t = steal_any(s, widx);
    if (t) {
      pthread_mutex_unlock(&s->lock);
      run_task(s, t);
      continue;
    }
    pthread_cond_wait(&s->cv, &s->lock);
    pthread_mutex_unlock(&s->lock);
  }
  return NULL;
}

/* ---- 公共 API ---- */
mosaic_sched *mosaic_sched_create(int n_workers) {
  if (n_workers < 1) return NULL;
  mosaic_sched *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  if (pthread_mutex_init(&s->lock, NULL) != 0 ||
      pthread_cond_init(&s->cv, NULL) != 0) { free(s); return NULL; }
  s->n_workers = n_workers;
  s->nbuckets = 64;
  s->buckets = calloc(s->nbuckets, sizeof *s->buckets);
  s->workers = calloc((size_t)n_workers, sizeof *s->workers);
  if (!s->buckets || !s->workers) { free(s->buckets); free(s->workers); free(s); return NULL; }
  /* 全部 worker 互斥锁先初始化、再建线程:worker 一启动就可能 trylock
     其他 worker 的锁(steal_any),初始化必须先于任何线程对锁的使用(TSan
     实证:pthread_mutex_init 与 trylock 竞争)。 */
  for (int i = 0; i < n_workers; i++) {
    if (pthread_mutex_init(&s->workers[i].lock, NULL) != 0) {
      for (int j = 0; j < i; j++) pthread_mutex_destroy(&s->workers[j].lock);
      free(s->buckets); free(s->workers); free(s);
      return NULL;
    }
    s->workers[i].sched = s;
  }
  for (int i = 0; i < n_workers; i++) {
    if (pthread_create(&s->workers[i].thread, NULL, worker_main, &s->workers[i]) != 0) {
      /* 部分线程已建:先停再回收 */
      s->stop = 1;
      pthread_mutex_lock(&s->lock);
      pthread_cond_broadcast(&s->cv);
      pthread_mutex_unlock(&s->lock);
      for (int j = 0; j < i; j++) pthread_join(s->workers[j].thread, NULL);
      for (int j = 0; j < n_workers; j++) pthread_mutex_destroy(&s->workers[j].lock);
      free(s->buckets); free(s->workers); free(s);
      return NULL;
    }
  }
  return s;
}

void mosaic_sched_destroy(mosaic_sched *s) {
  if (!s) return;
  mosaic_sched_wait_all(s);   /* 先等全部任务终态(取消级联保证不悬挂) */
  pthread_mutex_lock(&s->lock);
  s->stop = 1;
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->lock);
  for (int i = 0; i < s->n_workers; i++) {
    pthread_join(s->workers[i].thread, NULL);
    pthread_mutex_destroy(&s->workers[i].lock);
  }
  /* 释放:就绪集应已空;逐桶释放任务与依赖者数组 */
  for (u32 i = 0; i < s->nbuckets; i++) {
    mosaic_task *t = s->buckets[i];
    while (t) {
      mosaic_task *nx = t->hash_next;
      free(t->dependents);
      free(t);
      t = nx;
    }
  }
  free(s->buckets);
  free(s->workers);
  pthread_mutex_destroy(&s->lock);
  pthread_cond_destroy(&s->cv);
  free(s);
}

int mosaic_sched_submit(mosaic_sched *s, const mosaic_task_spec *spec) {
  if (!s || !spec || !spec->fn) return -1;
  if (spec->dep_count > MOSAIC_SCHED_MAX_DEPS) return -1;
  if (spec->affinity < -1 || spec->affinity >= s->n_workers) return -1;
  pthread_mutex_lock(&s->lock);
  if (table_find(s, spec->id)) { pthread_mutex_unlock(&s->lock); return -1; }
  for (u32 i = 0; i < spec->dep_count; i++)
    if (!table_find(s, spec->dep_ids[i])) { pthread_mutex_unlock(&s->lock); return -1; }

  mosaic_task *t = calloc(1, sizeof *t);
  if (!t) { pthread_mutex_unlock(&s->lock); return -1; }
  t->id = spec->id;
  t->dep_count = spec->dep_count;
  for (u32 i = 0; i < spec->dep_count; i++) t->dep_ids[i] = spec->dep_ids[i];
  t->priority = spec->priority;
  t->affinity = spec->affinity;
  t->fn = spec->fn;
  t->arg = spec->arg;
  t->checkpoint = spec->checkpoint;
  t->checkpoint_ctx = spec->checkpoint_ctx;
  t->owner = -1;
  t->submit_seq = s->submit_seq++;

  /* 统计未完成依赖;已取消的依赖 → 本任务直接取消 */
  u32 rem = 0;
  int doomed = 0;
  u32 edges = 0;   /* 已挂的依赖边数(失败时回滚) */
  for (u32 i = 0; i < spec->dep_count; i++) {
    mosaic_task *d = table_find(s, spec->dep_ids[i]);
    if (d->state == TASK_DONE) continue;
    if (d->state == TASK_CANCELLED) { doomed = 1; continue; }
    rem++;
    struct mosaic_task **nd = realloc(d->dependents,
                                      (size_t)(d->n_dependents + 1) * sizeof *nd);
    if (!nd) {
      for (u32 j = 0; j < edges; j++) {   /* 回滚已挂边,防悬挂指针 */
        mosaic_task *dd = table_find(s, spec->dep_ids[j]);
        if (dd->state != TASK_DONE && dd->state != TASK_CANCELLED)
          dd->n_dependents--;
      }
      free(t);
      pthread_mutex_unlock(&s->lock);
      return -1;
    }
    d->dependents = nd;
    d->dependents[d->n_dependents++] = t;
    edges++;
  }
  t->deps_remaining = rem;
  if (doomed) {
    t->state = TASK_CANCELLED;   /* 依赖在提交时已取消 → 无可执行性 */
    s->cancelled_total++;
    s->completed++;
  } else if (rem == 0) {
    t->state = TASK_READY;
    ready_insert(s, t);
  } else {
    t->state = TASK_PENDING;
  }
  table_insert(s, t);
  s->submitted++;
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->lock);
  return 0;
}

int mosaic_sched_wait_all(mosaic_sched *s) {
  if (!s) return -1;
  pthread_mutex_lock(&s->lock);
  while (s->completed < s->submitted)
    pthread_cond_wait(&s->cv, &s->lock);
  u64 cancelled = s->cancelled_total;
  pthread_mutex_unlock(&s->lock);
  return (int)cancelled;
}

int mosaic_sched_cancel(mosaic_sched *s, u64 task_id) {
  if (!s) return -1;
  pthread_mutex_lock(&s->lock);
  mosaic_task *t = table_find(s, task_id);
  if (!t || t->state == TASK_DONE || t->state == TASK_CANCELLED) {
    pthread_mutex_unlock(&s->lock);
    return -1;
  }
  if (t->state == TASK_RUNNING) {
    /* 运行中不可抢占:置标志,fn 返回后 checkpoint + 标 CANCELLED */
    t->cancelled = 1;
    pthread_mutex_unlock(&s->lock);
    return 0;
  }
  cancel_pending_locked(s, t);
  pthread_cond_broadcast(&s->cv);
  pthread_mutex_unlock(&s->lock);
  return 0;
}

u32 mosaic_sched_pending(mosaic_sched *s) {
  if (!s) return 0;
  pthread_mutex_lock(&s->lock);
  u32 n = (u32)(s->submitted - s->completed);
  pthread_mutex_unlock(&s->lock);
  return n;
}
