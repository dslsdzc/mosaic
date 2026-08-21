/* tests/test_sched.c — M2-3:DAG 任务调度器(线程池 + 依赖/优先级/亲和性/取消/checkpoint)
 *
 * 用例:
 *  - 无依赖 100 任务 → 全部执行,计数正确
 *  - 链式依赖 A→B→C → 执行序保证(A 先于 B 先于 C)
 *  - 菱形依赖(共同依赖 X)→ X 只执行一次
 *  - 优先级:同就绪集内高优先级先执行;平局按提交序(1 worker 确定序)
 *  - 亲和性:affinity=1/2 的任务各自只在单一 worker 线程上执行(线程绑定观测)
 *  - 取消:未开始 → 0、fn 不执行、wait_all 计数正确;运行中 → 0 + checkpoint
 *    回调;级联取消;未知 id / 已终态 → -1
 *  - 并发正确性:8 worker × 1000 任务随机依赖图(固定种子),pthread 超时看门狗
 *  - submit 参数错:未知依赖 / 重复 id / dep_count 越界 / affinity 越界 → -1
 *  - 集成演示:8 worker 并行物化 32 个冷函数 → 全部 ACTIVE(并行化的是实际工作)
 */
#define _GNU_SOURCE
#include "mosaic/base.h"
#include "mosaic/sched.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"
#include "mini_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

static const char *SO_PATH;   /* argv[1]:test_mod.so fixture(并行物化需要真实 dlopen) */

/* ---- 用例 1:无依赖 100 任务 ---- */
static _Atomic int g_bulk_exec[100];
static void fn_bulk(void *arg) {
  long i = (long)arg;
  atomic_fetch_add(&g_bulk_exec[i], 1);
}
static void test_no_dep_bulk(void) {
  const int N = 100;
  for (int i = 0; i < N; i++) atomic_init(&g_bulk_exec[i], 0);
  mosaic_sched *s = mosaic_sched_create(4);
  MT_CHECK(s != NULL);
  if (!s) return;
  for (int i = 0; i < N; i++) {
    mosaic_task_spec sp = { .id = 1000 + i, .dep_count = 0, .priority = 0,
                            .affinity = -1, .fn = fn_bulk, .arg = (void *)(long)i };
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  MT_CHECK_EQ_U64(mosaic_sched_pending(s), 0);
  int sum = 0;
  for (int i = 0; i < N; i++) sum += atomic_load(&g_bulk_exec[i]);
  MT_CHECK_EQ_U64(sum, N);
  mosaic_sched_destroy(s);
}

/* ---- 用例 2:链式依赖 A→B→C ---- */
static _Atomic long g_chain_seq[3], g_chain_order;
static void fn_chain(void *arg) {
  long i = (long)arg;
  long n = atomic_fetch_add(&g_chain_order, 1);
  atomic_store(&g_chain_seq[i], n);
}
static void test_chain_deps(void) {
  for (int i = 0; i < 3; i++) { atomic_init(&g_chain_seq[i], -1); }
  atomic_init(&g_chain_order, 0);
  mosaic_sched *s = mosaic_sched_create(4);
  MT_CHECK(s != NULL);
  if (!s) return;
  for (int i = 0; i < 3; i++) {
    mosaic_task_spec sp = { .id = 10 + i, .dep_count = i ? 1 : 0,
      .dep_ids = { i ? (u32)(10 + i - 1) : 0 }, .priority = 0, .affinity = -1,
      .fn = fn_chain, .arg = (void *)(long)i };
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  /* B 只能在 A 完成传播后开始 → 开始序严格递增 */
  MT_CHECK(atomic_load(&g_chain_seq[0]) < atomic_load(&g_chain_seq[1]));
  MT_CHECK(atomic_load(&g_chain_seq[1]) < atomic_load(&g_chain_seq[2]));
  mosaic_sched_destroy(s);
}

/* ---- 用例 3:菱形依赖(共同依赖只执行一次) ---- */
static _Atomic int g_di_exec[3];
static void fn_di(void *arg) {
  long i = (long)arg;
  atomic_fetch_add(&g_di_exec[i], 1);
}
static void test_diamond(void) {
  for (int i = 0; i < 3; i++) atomic_init(&g_di_exec[i], 0);
  mosaic_sched *s = mosaic_sched_create(4);
  MT_CHECK(s != NULL);
  if (!s) return;
  /* X=20;A=21→X;B=22→X */
  mosaic_task_spec sp[3] = {
    { .id = 20, .dep_count = 0, .priority = 0, .affinity = -1, .fn = fn_di, .arg = (void *)0 },
    { .id = 21, .dep_ids = { 20 }, .dep_count = 1, .priority = 0, .affinity = -1, .fn = fn_di, .arg = (void *)1 },
    { .id = 22, .dep_ids = { 20 }, .dep_count = 1, .priority = 0, .affinity = -1, .fn = fn_di, .arg = (void *)2 },
  };
  for (int i = 0; i < 3; i++) MT_CHECK(mosaic_sched_submit(s, &sp[i]) == 0);
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  MT_CHECK_EQ_U64(atomic_load(&g_di_exec[0]), 1);   /* X 只执行一次 */
  MT_CHECK_EQ_U64(atomic_load(&g_di_exec[1]), 1);
  MT_CHECK_EQ_U64(atomic_load(&g_di_exec[2]), 1);
  mosaic_sched_destroy(s);
}

/* ---- 用例 4:优先级(1 worker 确定序;平局按提交序) ---- */
static _Atomic int g_pri_order[8], g_pri_n;
static void fn_pri(void *arg) {
  long id = (long)arg;
  int n = atomic_fetch_add(&g_pri_n, 1);
  if (n < 8) atomic_store(&g_pri_order[n], (int)id);
}
static void test_priority(void) {
  atomic_init(&g_pri_n, 0);
  for (int i = 0; i < 8; i++) atomic_init(&g_pri_order[i], -1);
  mosaic_sched *s = mosaic_sched_create(1);
  MT_CHECK(s != NULL);
  if (!s) return;
  /* 提交序:T3(p=3), T1(p=1), T2(p=2), T4(p=2,与 T2 平局但提交更晚) */
  struct { u64 id; int pri; } plan[4] = { {3,3}, {1,1}, {2,2}, {4,2} };
  for (int i = 0; i < 4; i++) {
    mosaic_task_spec sp = { .id = plan[i].id, .dep_count = 0,
      .priority = plan[i].pri, .affinity = -1, .fn = fn_pri, .arg = (void *)(long)plan[i].id };
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  /* 期望序:3(高优先), 2(与 4 平局,提交先), 4, 1(低优先) */
  MT_CHECK_EQ_U64(g_pri_order[0], 3);
  MT_CHECK_EQ_U64(g_pri_order[1], 2);
  MT_CHECK_EQ_U64(g_pri_order[2], 4);
  MT_CHECK_EQ_U64(g_pri_order[3], 1);
  mosaic_sched_destroy(s);
}

/* ---- 用例 5:亲和性(线程绑定观测) ----
 * fn 拿不到 worker 下标(调度器内部),但可记录 pthread_self():
 * affinity=1 的任务集合必须全部出现在同一线程上(且与 affinity=2 的
 * 集合线程不同,排除"只有 1 个 worker"假阳性);affinity=-1 的任务
 * 分散 ≥2 个线程(多 worker 并行,排除单线程假阳性)。-1 任务带 1ms
 * 睡眠,给其他 worker 醒来领批次的窗口,保证任务确实分散。 */
static _Atomic unsigned long g_aff_thr[80];
static void fn_aff(void *arg) {
  long id = (long)arg;
  atomic_store(&g_aff_thr[id - 300], (unsigned long)pthread_self());
  if (id >= 340) usleep(1000);   /* -1 任务慢一点,让多 worker 参与 */
}
static void test_affinity(void) {
  for (int i = 0; i < 80; i++) atomic_init(&g_aff_thr[i], 0);
  mosaic_sched *s = mosaic_sched_create(4);
  MT_CHECK(s != NULL);
  if (!s) return;
  /* ids 300-319 → affinity=1;320-339 → affinity=2;340-379 → affinity=-1 */
  for (u64 id = 300; id < 380; id++) {
    int aff = (id < 320) ? 1 : (id < 340) ? 2 : -1;
    mosaic_task_spec sp = { .id = id, .dep_count = 0, .priority = 0,
                            .affinity = aff, .fn = fn_aff, .arg = (void *)(long)id };
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  /* 分三段收集线程集合 */
  int n1 = 0, n2 = 0, nf = 0;
  unsigned long thr1[32], thr2[32], thrf[48];
  for (int i = 0; i < 80; i++) {
    unsigned long t = atomic_load(&g_aff_thr[i]);
    if (i < 20) thr1[n1++] = t;
    else if (i < 40) thr2[n2++] = t;
    else thrf[nf++] = t;
  }
  unsigned long u1 = thr1[0], u2 = thr2[0];
  int same1 = 1, same2 = 1;
  for (int i = 1; i < n1; i++) if (thr1[i] != u1) same1 = 0;
  for (int i = 1; i < n2; i++) if (thr2[i] != u2) same2 = 0;
  MT_CHECK(same1);                       /* affinity=1 全在同一线程 */
  MT_CHECK(same2);                       /* affinity=2 全在同一线程 */
  MT_CHECK(u1 != u2);                    /* 两个固定 worker 不同 */
  /* affinity=-1 的任务分散 ≥2 个线程 */
  int distinct = 0;
  for (int i = 0; i < nf; i++) { int seen = 0; for (int j = 0; j < i; j++) if (thrf[j] == thrf[i]) seen = 1; if (!seen) distinct++; }
  MT_CHECK(distinct >= 2);
  mosaic_sched_destroy(s);
}

/* ---- 用例 6:取消 ----
 * 场景 A(未开始任务,1 worker 定时):fn 睡眠 20ms;提交 11 个任务后 60ms
 * 时(worker 至多跑到第 4 个,105+ 必未开始)取消 105/107/109/110:
 *   返回 0、fn 不执行、pending 立减、wait_all 返回 4。
 * 场景 B(运行中取消 + checkpoint):单任务 fn 睡眠 200ms,50ms 时取消 →
 *   0;fn 照常执行完;checkpoint 被调用;wait_all 返回 1;再次取消 → -1。
 * 场景 C(级联取消):链 200→201→202→203,任务 200 运行中取消 →
 *   201/202/203 级联取消不执行(级联不回调 checkpoint)。
 * 场景 D:未知 id → -1;已终态 → -1。 */
static _Atomic int g_cx_exec[16];
static _Atomic int g_cx_checkpoint;
static void fn_cx(void *arg) {
  long i = (long)arg;
  atomic_fetch_add(&g_cx_exec[i], 1);
}
static void fn_cx_med(void *arg) {    /* 20ms:场景 A 的未开始窗口 */
  long i = (long)arg;
  usleep(20000);
  atomic_fetch_add(&g_cx_exec[i], 1);
}
static void fn_cx_slow(void *arg) {   /* 200ms:场景 B/C 的"运行中取消"窗口 */
  long i = (long)arg;
  usleep(200000);
  if (i) atomic_fetch_add(&g_cx_exec[i], 1);   /* i==0(场景 C 的 200)不计数 */
}
static void ck_cx(mosaic_task *t, void *ctx) { (void)t; (void)ctx; atomic_fetch_add(&g_cx_checkpoint, 1); }

static void test_cancel(void) {
  for (int i = 0; i < 16; i++) atomic_init(&g_cx_exec[i], 0);
  atomic_init(&g_cx_checkpoint, 0);

  /* 场景 A:未开始任务取消 */
  {
    mosaic_sched *s = mosaic_sched_create(1);
    MT_CHECK(s != NULL);
    if (s) {
      for (u64 i = 0; i < 11; i++) {
        mosaic_task_spec sp = { .id = 100 + i, .dep_count = 0, .priority = 0,
                                .affinity = -1, .fn = fn_cx_med, .arg = (void *)(long)i };
        MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
      }
      usleep(60000);   /* 20ms/任务:60ms 时至多完成 3 个,105/107/109/110 必未开始 */
      /* 取消 105,107,109,110(均未开始)→ 0 */
      MT_CHECK(mosaic_sched_cancel(s, 105) == 0);
      MT_CHECK(mosaic_sched_cancel(s, 107) == 0);
      MT_CHECK(mosaic_sched_cancel(s, 109) == 0);
      MT_CHECK(mosaic_sched_cancel(s, 110) == 0);
      u32 pend = mosaic_sched_pending(s);
      MT_CHECK(pend >= 4 && pend <= 7);  /* 11 - 4 已取消 - (0..3) 已完成 */
      MT_CHECK(mosaic_sched_cancel(s, 999) == -1);   /* 未知 id */
      MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 4);  /* 4 个被取消的任务 */
      /* 被取消的 fn 未执行;其余(包括已跑完的)全部执行一次 */
      for (u64 i = 0; i < 11; i++) {
        int cancelled = (i == 5 || i == 7 || i == 9 || i == 10);
        MT_CHECK_EQ_U64(atomic_load(&g_cx_exec[i]), cancelled ? 0 : 1);
      }
      MT_CHECK(mosaic_sched_cancel(s, 100) == -1); /* 已完成 → -1 */
      mosaic_sched_destroy(s);
    }
  }
  /* 场景 B:运行中取消 + checkpoint(独立计数槽 15,不与场景 A 的 101 冲突) */
  {
    mosaic_sched *s = mosaic_sched_create(1);
    MT_CHECK(s != NULL);
    if (s) {
      mosaic_task_spec sp = { .id = 1, .dep_count = 0, .priority = 0, .affinity = -1,
                              .fn = fn_cx_slow, .arg = (void *)15, .checkpoint = ck_cx,
                              .checkpoint_ctx = NULL };
      MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
      usleep(50000);   /* 50ms < 200ms:必在运行中 */
      MT_CHECK(mosaic_sched_cancel(s, 1) == 0);
      MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 1);   /* 以取消收尾,计入返回值 */
      MT_CHECK_EQ_U64(atomic_load(&g_cx_checkpoint), 1);  /* checkpoint 被调用 */
      MT_CHECK_EQ_U64(atomic_load(&g_cx_exec[15]), 1);    /* fn 执行完了 */
      MT_CHECK(mosaic_sched_cancel(s, 1) == -1);      /* 已终态 */
      mosaic_sched_destroy(s);
    }
  }
  /* 场景 C:级联取消(链 200→201→202→203,运行中取消 → 依赖者全部级联取消) */
  {
    atomic_store(&g_cx_checkpoint, 0);   /* 本场景独立计数 */
    mosaic_sched *s = mosaic_sched_create(1);
    MT_CHECK(s != NULL);
    if (s) {
      for (u64 i = 0; i < 4; i++) {
        mosaic_task_spec sp = { .id = 200 + i, .dep_count = i ? 1 : 0,
          .dep_ids = { i ? (u32)(200 + i - 1) : 0 }, .priority = 0, .affinity = -1,
          .fn = fn_cx_slow, .arg = NULL, .checkpoint = ck_cx, .checkpoint_ctx = NULL };
        MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
      }
      usleep(30000);   /* 30ms < 200ms:任务 200 运行中,201+ 未开始 */
      MT_CHECK(mosaic_sched_cancel(s, 200) == 0);
      MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 4);   /* 200 运行中取消 + 201/202/203 级联 */
      MT_CHECK_EQ_U64(atomic_load(&g_cx_checkpoint), 1);  /* 仅 200 运行中取消回调;级联不回调 */
      mosaic_sched_destroy(s);
    }
  }
  /* 场景 D:依赖在提交时已取消 → 新任务以取消收尾(提交即终态,不执行) */
  {
    mosaic_sched *s = mosaic_sched_create(1);
    MT_CHECK(s != NULL);
    if (s) {
      mosaic_task_spec x = { .id = 500, .dep_count = 0, .priority = 0, .affinity = -1,
                             .fn = fn_cx_med, .arg = (void *)11 };
      MT_CHECK(mosaic_sched_submit(s, &x) == 0);
      usleep(5000);   /* 5ms < 20ms:任务 500 运行中 */
      MT_CHECK(mosaic_sched_cancel(s, 500) == 0);
      mosaic_task_spec y = { .id = 501, .dep_ids = { 500 }, .dep_count = 1,
                             .priority = 0, .affinity = -1,
                             .fn = fn_cx_med, .arg = (void *)12 };
      MT_CHECK(mosaic_sched_submit(s, &y) == 0);   /* 提交成功,但依赖已取消 */
      MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 2);   /* 500 运行中取消 + 501 提交即取消 */
      MT_CHECK_EQ_U64(atomic_load(&g_cx_exec[11]), 1);  /* 500 的 fn 执行完 */
      MT_CHECK_EQ_U64(atomic_load(&g_cx_exec[12]), 0);  /* 501 从未执行 */
      mosaic_sched_destroy(s);
    }
  }
}

/* ---- 用例 7:并发正确性:8 worker × 1000 任务随机依赖图(固定种子) ----
 * wait_all 在 watchdog 线程执行,pthread_timedjoin_np 超时 → 死锁即失败。 */
static _Atomic int g_rand_exec[1000];
static void fn_rand(void *arg) {
  long i = (long)arg;
  atomic_fetch_add(&g_rand_exec[i], 1);
}
typedef struct { mosaic_sched *s; int rc; } wait_ctx;
static void *wait_all_thread(void *arg) {
  wait_ctx *c = arg;
  c->rc = mosaic_sched_wait_all(c->s);
  return NULL;
}
static void test_concurrent_random(void) {
  const int N = 1000;
  for (int i = 0; i < N; i++) atomic_init(&g_rand_exec[i], 0);
  /* 固定种子 LCG */
  unsigned long rng = 42;
  mosaic_sched *s = mosaic_sched_create(8);
  MT_CHECK(s != NULL);
  if (!s) return;
  for (int i = 0; i < N; i++) {
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;
    u32 ndeps = (u32)((rng >> 33) % 4);   /* 0..3 个依赖,指向更小 id → 无环 */
    if (i == 0) ndeps = 0;                /* 无更小 id 可依赖(禁自环) */
    mosaic_task_spec sp = { .id = 5000 + i, .dep_count = ndeps, .priority = (int)(rng & 7),
                            .affinity = -1, .fn = fn_rand, .arg = (void *)(long)i };
    for (u32 d = 0; d < ndeps; d++) {
      rng = rng * 6364136223846793005ull + 1442695040888963407ull;
      sp.dep_ids[d] = (u32)(5000 + (rng >> 33) % (u64)i);   /* [5000, 5000+i) */
    }
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  wait_ctx c = { s, -1 };
  pthread_t th;
  MT_CHECK(pthread_create(&th, NULL, wait_all_thread, &c) == 0);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += 30;   /* 超时看门狗:卡死 → 测试失败 */
  int jr = pthread_timedjoin_np(th, NULL, &ts);
  if (jr != 0) {
    fprintf(stderr, "FAIL: wait_all watchdog timeout (deadlock?)\n");
    mt_failures++;
    _exit(1);        /* 调度器卡死,无法清理,直接退出(本用例置于末位之前) */
  }
  MT_CHECK_EQ_U64(c.rc, 0);              /* 无取消 → 0 */
  MT_CHECK_EQ_U64(mosaic_sched_pending(s), 0);
  int sum = 0;
  for (int i = 0; i < N; i++) sum += atomic_load(&g_rand_exec[i]);
  MT_CHECK_EQ_U64(sum, N);               /* 每个任务恰好执行一次 */
  mosaic_sched_destroy(s);
}

/* ---- 用例 8:submit 参数错 ---- */
static void test_submit_errors(void) {
  mosaic_sched *s = mosaic_sched_create(4);
  MT_CHECK(s != NULL);
  if (!s) return;
  mosaic_task_spec sp = { .id = 1, .dep_count = 0, .priority = 0, .affinity = -1,
                          .fn = fn_rand, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  /* 未知依赖 */
  mosaic_task_spec bad = { .id = 2, .dep_ids = { 999 }, .dep_count = 1,
                           .priority = 0, .affinity = -1, .fn = fn_rand, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &bad) == -1);
  /* 重复 id */
  MT_CHECK(mosaic_sched_submit(s, &sp) == -1);
  /* dep_count 越界 */
  mosaic_task_spec over = { .id = 3, .dep_count = MOSAIC_SCHED_MAX_DEPS + 1,
                            .priority = 0, .affinity = -1, .fn = fn_rand, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &over) == -1);
  /* affinity 越界(4 worker:下标 0..3;-1 任意) */
  mosaic_task_spec aff_bad = { .id = 4, .dep_count = 0, .priority = 0,
                               .affinity = 4, .fn = fn_rand, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &aff_bad) == -1);
  mosaic_task_spec aff_bad2 = { .id = 5, .dep_count = 0, .priority = 0,
                                .affinity = -2, .fn = fn_rand, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &aff_bad2) == -1);
  /* fn 空 */
  mosaic_task_spec nofn = { .id = 6, .dep_count = 0, .priority = 0,
                            .affinity = -1, .fn = NULL, .arg = NULL };
  MT_CHECK(mosaic_sched_submit(s, &nofn) == -1);
  MT_CHECK(mosaic_sched_submit(NULL, &sp) == -1);
  MT_CHECK(mosaic_sched_submit(s, NULL) == -1);
  MT_CHECK(mosaic_sched_create(0) == NULL);
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  mosaic_sched_destroy(s);
}

/* ---- 用例 9:集成演示:8 worker 并行物化 32 个冷函数 ---- */
static pthread_mutex_t g_rt_lock;   /* runtime 尚未内置锁(M2 后续任务加);调度器
                                       本身全并行,这把锁是调用方接缝 */
static struct mosaic_runtime *g_rt;
static _Atomic int g_mat_fail;
static void fn_mat(void *arg) {
  long base = (long)arg;
  for (long o = 0; o < 4; o++) {
    pthread_mutex_lock(&g_rt_lock);
    mosaic_fn_obj *f = mosaic_fn_materialize(g_rt, (10ull << 32) | (u64)(base + o));
    pthread_mutex_unlock(&g_rt_lock);
    if (!f) atomic_fetch_add(&g_mat_fail, 1);
  }
}
static void test_parallel_materialize(void) {
  if (!SO_PATH) { fprintf(stderr, "  (skip: no SO fixture)\n"); return; }
  char err[256];
  const char *path = "/tmp/mosaic_sched_mat.pack";
  mosaic_pack_builder *b = mosaic_pack_builder_create(path, 1, 32, 0, 0, 1);
  if (!b) { MT_CHECK(0); return; }
  mosaic_pack_builder_add_event(b, "tick");
  mosaic_pack_builder_add_module(b, 10, 1, "mod", SO_PATH);
  for (u64 local = 0; local < 32; local++)
    mosaic_pack_builder_add_fn(b, 10, local, 0, 64, 0, 0,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  mosaic_pack_builder_free(b);
  MT_CHECK(rc == 0);
  if (rc) { fprintf(stderr, "build: %s\n", err); return; }

  mosaic_runtime *rt = mosaic_runtime_open(path, err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) { fprintf(stderr, "open: %s\n", err); return; }
  g_rt = rt;
  pthread_mutex_init(&g_rt_lock, NULL);
  atomic_init(&g_mat_fail, 0);

  mosaic_sched *s = mosaic_sched_create(8);
  MT_CHECK(s != NULL);
  if (!s) { mosaic_runtime_close(rt); return; }
  /* 8 个任务,每个物化 4 个冷函数(切片) */
  for (long k = 0; k < 8; k++) {
    mosaic_task_spec sp = { .id = 400 + k, .dep_count = 0, .priority = 0,
                            .affinity = -1, .fn = fn_mat, .arg = (void *)(long)(k * 4) };
    MT_CHECK(mosaic_sched_submit(s, &sp) == 0);
  }
  MT_CHECK_EQ_U64(mosaic_sched_wait_all(s), 0);
  MT_CHECK_EQ_U64(atomic_load(&g_mat_fail), 0);   /* 全部物化成功 */
  /* 断言 32 个函数全部 ACTIVE(记录状态标志) */
  int active = 0;
  for (u64 local = 0; local < 32; local++) {
    size_t pack = 0;
    const mosaic_function_record *rec = find_function_active(rt, (10ull << 32) | local, &pack);
    if (rec && (mf_flags(rec) & MOSAIC_FN_STATE_MASK) == MOSAIC_FN_STATE_ACTIVE) active++;
  }
  MT_CHECK_EQ_U64(active, 32);
  mosaic_sched_destroy(s);
  pthread_mutex_destroy(&g_rt_lock);
  mosaic_runtime_close(rt);
}

int main(int argc, char **argv) {
  if (argc > 1) SO_PATH = argv[1];
  MT_RUN(test_no_dep_bulk);
  MT_RUN(test_chain_deps);
  MT_RUN(test_diamond);
  MT_RUN(test_priority);
  MT_RUN(test_affinity);
  MT_RUN(test_cancel);
  MT_RUN(test_submit_errors);
  MT_RUN(test_parallel_materialize);
  MT_RUN(test_concurrent_random);   /* 末位:watchdog 超时用 _exit 兜底 */
  fprintf(stderr, "test_sched: %s\n", MT_RESULT() ? "PASS" : "FAIL");
  return MT_RESULT() ? 0 : 1;
}
