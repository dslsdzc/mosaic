/* bench/gen_world_pack.c — M4-2:1.20.1 服务端世界 pack 生成器。
 *
 * 默认模式(world.pack):1 模块 × 27 函数(libtest_mod.so 的 code_off 0 =
 * code_inc,计数 state),12 个事件 × 每事件 2-3 个订阅函数(计数 state):
 *   player_join(2)、player_leave(2)、block_break(3)、tick(3)、
 *   server_command(3)、block_place(2)、entity_spawn(2)、
 *   player_chat(2)、player_death(2)、player_command(2)、
 *   packet_received(2)、packet_sent(2)
 * ——事件名与 agent 注入的 hook 派发名(com.mosaic.agent.MosaicHooks)一致,
 *   派发计数可在 /mosaic status 观察(每订阅者一次执行)。
 *   (M8-D:事件集加入 block_place/entity_spawn/player_chat/player_death,
 *   对应 M8-D 新增 4 个注入 hook;Task 5:加入 player_command,对应
 *   onChatCommand 的 chat 命令漏斗派发;Task 6:加入 packet_received/
 *   packet_sent,对应 Connection 双向挂钩)
 *
 * [world2] 模式(M4-3:世界内动态加载验证 pack):模块 2 × 6 函数
 * (tick × 3、player_join × 3,全部 code_off 0 计数 state),事件表与
 * world.pack 完全一致(10 事件同名同序)→ 挂载校验通过;模块 id 2 不与
 * world.pack 的模块 1 重叠 → 安装后下个 tick 即派发(world2 的 tick 订阅者
 * 随 tick 执行,/mosaic status 的 tick executed 增长速率翻倍可见)。
 *
 * 用法:gen_world_pack <out.pack> <module.so> [world2]
 */
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { N_EV = 12, N_FN = 27, N_TRIG = 27 };
enum { N2_FN = 6, N2_TRIG = 6 };

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <out.pack> <module.so> [world2]\n", argv[0]);
    return 2;
  }
  int world2 = argc > 3 && strcmp(argv[3], "world2") == 0;
  char err[256];

  static const char *ev_names[N_EV] = {
    "player_join", "player_leave", "block_break", "tick", "server_command",
    "block_place", "entity_spawn", "player_chat", "player_death",
    "player_command", "packet_received", "packet_sent",
  };

  if (world2) {
    /* 模块 2 × 6 函数:tick × 3、player_join × 3(每事件 3 订阅,计数 state) */
    mosaic_pack_builder *b =
        mosaic_pack_builder_create(argv[1], 1, N2_FN, N2_TRIG, 0, N_EV);
    if (!b) { fprintf(stderr, "gen_world_pack: builder create failed\n"); return 1; }
    mosaic_pack_builder_add_module(b, 2, 1, "world2_mod", argv[2]);
    for (u32 ev = 0; ev < N_EV; ev++)
      mosaic_pack_builder_add_event(b, ev_names[ev]);
    /* tick = 3 个订阅函数,player_join = 3 个订阅函数;其余事件无订阅 */
    u32 local = 0;
    for (u32 ev = 0; ev < N_EV; ev++) {
      int subs = (strcmp(ev_names[ev], "tick") == 0 ||
                  strcmp(ev_names[ev], "player_join") == 0) ? 3 : 0;
      for (int k = 0; k < subs; k++) {
        mosaic_pack_builder_add_fn(b, 2, local, 0, 64, 1, 1,
                                   MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
        mosaic_pack_builder_add_trigger(b, ev, 0x200000000ull | local);
        local++;
      }
    }
    if (local != N2_FN) { fprintf(stderr, "gen_world_pack: fn count mismatch\n"); return 1; }
    int rc = mosaic_pack_builder_finish(b, err, sizeof err);
    if (rc) fprintf(stderr, "gen_world_pack: %s\n", err);
    mosaic_pack_builder_free(b);
    return rc ? 1 : 0;
  }

  /* 1 模块 27 函数 27 触发器 0 依赖 12 事件 */
  mosaic_pack_builder *b =
      mosaic_pack_builder_create(argv[1], 1, N_FN, N_TRIG, 0, N_EV);
  if (!b) { fprintf(stderr, "gen_world_pack: builder create failed\n"); return 1; }

  static const u32 ev_subs[N_EV] = { 2, 2, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2 };   /* 每事件订阅函数数 */

  mosaic_pack_builder_add_module(b, 1, 1, "world_mod", argv[2]);

  /* 10 个事件各注册一次(注册序 0..9;builder 内部按名排序重映射触发器) */
  for (u32 ev = 0; ev < N_EV; ev++)
    mosaic_pack_builder_add_event(b, ev_names[ev]);

  /* 27 个计数函数(全部 code_off 0 = code_inc,state 64B:counter++/last_event) */
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
