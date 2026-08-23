#include "mosaic/base.h"
#include "mosaic/function.h"
#include "mosaic_internal.h"
#include "mini_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_ws_basic(void) {
  char err[256];
  mosaic_pack_builder *b = mosaic_pack_builder_create("/tmp/mosaic_test_ws.pack", 1, 100, 0, 0, 0);
  mosaic_pack_builder_add_module(b, 10, 1, "m", "/tmp/x.so");
  for (int i = 0; i < 100; i++)
    mosaic_pack_builder_add_fn(b, 10, (u64)i, 0, 0, 1, 0, 0);
  if (mosaic_pack_builder_finish(b, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); }
  mosaic_pack_builder_free(b);
  mosaic_runtime *rt = mosaic_runtime_open("/tmp/mosaic_test_ws.pack", err, sizeof err);
  MT_CHECK(rt != NULL);
  if (!rt) return;
  /* 插入 1000 个(触发扩容) */
  for (u64 i = 0; i < 1000; i++) {
    struct mosaic_fn_obj *f = fn_alloc(rt);
    MT_CHECK(f != NULL);
    f->fn_id = i + 1;
    ws_insert(rt, f);
  }
  MT_CHECK_EQ_U64(rt->ws.len, 1000);
  for (u64 i = 0; i < 1000; i++) {
    struct mosaic_fn_obj *f = ws_find(rt, i + 1);
    MT_CHECK(f != NULL && f->fn_id == i + 1);
  }
  MT_CHECK(ws_find(rt, 999999) == NULL);
  /* 删除一半 */
  for (u64 i = 0; i < 500; i++) ws_remove(rt, ws_find(rt, i * 2 + 1));
  MT_CHECK_EQ_U64(rt->ws.len, 500);
  MT_CHECK(ws_find(rt, 1) == NULL);
  MT_CHECK(ws_find(rt, 2) != NULL);
  /* 窗口链表一致性 */
  u64 walked = 0;
  for (struct mosaic_fn_obj *f = rt->ws_head; f; f = f->next) walked++;
  MT_CHECK_EQ_U64(walked, 500);
  /* 释放:再分配复用 */
  fn_free(rt, ws_find(rt, 2));
  struct mosaic_fn_obj *f2 = fn_alloc(rt);
  MT_CHECK(f2 != NULL);
  /* 清理 */
  for (struct mosaic_fn_obj *f = rt->ws_head; f; f = f->next) fn_free(rt, f);
  mosaic_runtime_close(rt);
}

static void test_arena(void) {
  void *p1 = NULL; void *p2 = NULL;
  /* 直接构造最小 runtime 测试 arena_alloc 语义(不依赖 pack) */
  mosaic_runtime rt; memset(&rt, 0, sizeof rt);
  arena_zalloc(&rt, 64, &p1);
  MT_CHECK(p1 != NULL);
  for (int i = 0; i < 64; i++) MT_CHECK(((u8 *)p1)[i] == 0);
  p2 = arena_alloc(&rt, 1024);
  MT_CHECK(p2 != NULL);
  free(p1); free(p2);
}

/* 栈上直接构造的 runtime 不走 open/close 生命周期(close 末尾会 free(rt),
   不可用于栈对象),其持有的堆资源(slab 区 + ws 数组)须在测试收尾手动释放
   ——否则 build-asan 报泄漏(1.3:修前 ws_grow 2×128B + slab 64K+40B)。 */
static void stack_rt_close(mosaic_runtime *rt) {
  for (struct slab *s = rt->slabs; s; ) {
    struct slab *nx = s->next;
    free(s->start); free(s);
    s = nx;
  }
  free(rt->ws.keys);
  free(rt->ws.vals);
}

static void test_cluster_removal(void) {
  /* 开放寻址簇删除回归测试:cap=16,键位混合后 id 1/17/33 仍同余碰撞成簇
     (三个键落同一槽,线性探测占连续三槽)。删除中间的 17 后,33 必须仍然
     可查(后移修复)。 */
  mosaic_runtime rt; memset(&rt, 0, sizeof rt);
  for (u64 id = 1; id <= 33; id += 16) {   /* 1, 17, 33 */
    struct mosaic_fn_obj *f = fn_alloc(&rt);
    MT_CHECK(f != NULL);
    f->fn_id = id;
    ws_insert(&rt, f);
  }
  MT_CHECK_EQ_U64(rt.ws.cap, 16);   /* 3 个元素不触发扩容 */
  struct mosaic_fn_obj *mid = ws_find(&rt, 17);
  MT_CHECK(mid != NULL);
  ws_remove(&rt, mid);
  MT_CHECK(ws_find(&rt, 1) != NULL);
  MT_CHECK(ws_find(&rt, 33) != NULL);   /* 无修复时这里返回 NULL */
  MT_CHECK(ws_find(&rt, 17) == NULL);
  /* 清空资源 */
  for (struct mosaic_fn_obj *f = rt.ws_head; f; f = f->next) fn_free(&rt, f);
  stack_rt_close(&rt);   /* 1.3:释放栈 runtime 的 slab + ws 数组(ASan 泄漏基线) */
}

int main(void) {
  MT_RUN(test_ws_basic);
  MT_RUN(test_arena);
  MT_RUN(test_cluster_removal);
  return MT_RESULT() ? 0 : 1;
}
