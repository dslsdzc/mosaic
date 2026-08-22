/* bench/gen_world_pack.c — M4-2:1.20.1 服务端世界 pack 生成器。
 *
 * 1 模块 × 13 函数(libtest_mod.so 的 code_off 0 = code_inc,计数 state),
 * 5 个事件 × 每事件 2-3 个订阅函数(计数 state):
 *   player_join(2)、player_leave(2)、block_break(3)、tick(3)、server_command(3)
 * ——事件名与 agent 注入的 hook 派发名(com.mosaic.agent.MosaicHooks)一致,
 *   派发计数可在 /mosaic status 观察(每订阅者一次执行)。
 *
 * 用法:gen_world_pack <out.pack> <module.so>
 */
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>

enum { N_EV = 5, N_FN = 13, N_TRIG = 13 };

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <out.pack> <module.so>\n", argv[0]);
    return 2;
  }
  char err[256];
  /* 1 模块 13 函数 13 触发器 0 依赖 5 事件 */
  mosaic_pack_builder *b =
      mosaic_pack_builder_create(argv[1], 1, N_FN, N_TRIG, 0, N_EV);
  if (!b) { fprintf(stderr, "gen_world_pack: builder create failed\n"); return 1; }

  static const char *ev_names[N_EV] = {
    "player_join", "player_leave", "block_break", "tick", "server_command",
  };
  static const u32 ev_subs[N_EV] = { 2, 2, 3, 3, 3 };   /* 每事件订阅函数数 */

  mosaic_pack_builder_add_module(b, 1, 1, "world_mod", argv[2]);

  /* 5 个事件各注册一次(注册序 0..4;builder 内部按名排序重映射触发器) */
  for (u32 ev = 0; ev < N_EV; ev++)
    mosaic_pack_builder_add_event(b, ev_names[ev]);

  /* 13 个计数函数(全部 code_off 0 = code_inc,state 64B:counter++/last_event) */
  u32 local = 0;
  for (u32 ev = 0; ev < N_EV; ev++) {
    for (u32 k = 0; k < ev_subs[ev]; k++) {
      mosaic_pack_builder_add_fn(b, 1, local, 0, 64, 1, 1,
                                 MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
      mosaic_pack_builder_add_trigger(b, ev, 0x100000000ull | local);
      local++;
    }
  }
  if (local != N_FN) { fprintf(stderr, "gen_world_pack: fn count mismatch\n"); return 1; }

  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "gen_world_pack: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc ? 1 : 0;
}
