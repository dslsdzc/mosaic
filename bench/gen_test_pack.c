/* bench/gen_test_pack.c — M4-1:JNI 测试用最小 pack 生成器。
 *
 * 1 模块 × 3 函数(so 指向合成模块,synth_abi.c 恰好 3 个代码入口 code 0/1/2),
 * 1 个事件 player_join + 2 个触发器(订阅 fn(1,0) 与 fn(1,1))→
 * dispatch(player_join) 执行数 == 2、工作集 == 2。
 *
 * 用法:gen_test_pack <out.pack> <module.so>
 */
#include "mosaic/pack.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <out.pack> <module.so>\n", argv[0]);
    return 2;
  }
  char err[256];
  mosaic_pack_builder *b =
      mosaic_pack_builder_create(argv[1], 1, 3, 2, 0, 1);   /* 1 模块 3 函数 2 触发器 1 事件 */
  if (!b) { fprintf(stderr, "gen_test_pack: builder create failed\n"); return 1; }
  mosaic_pack_builder_add_event(b, "player_join");          /* 单事件 → id 0 */
  mosaic_pack_builder_add_module(b, 1, 1, "jni_mod", argv[2]);
  for (u32 local = 0; local < 3; local++)
    mosaic_pack_builder_add_fn(b, 1, local, local, 64, 1, 1,
                               MOSAIC_FN_REQUIRES_STATE | MOSAIC_FN_TOMBSTONE_ABLE);
  mosaic_pack_builder_add_trigger(b, 0, 0x100000000ull);    /* fn(1,0) → player_join */
  mosaic_pack_builder_add_trigger(b, 0, 0x100000001ull);    /* fn(1,1) → player_join */
  int rc = mosaic_pack_builder_finish(b, err, sizeof err);
  if (rc) fprintf(stderr, "gen_test_pack: %s\n", err);
  mosaic_pack_builder_free(b);
  return rc ? 1 : 0;
}
