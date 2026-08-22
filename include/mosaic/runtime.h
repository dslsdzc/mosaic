#ifndef MOSAIC_RUNTIME_H
#define MOSAIC_RUNTIME_H
#include "mosaic/pack.h"

typedef struct mosaic_runtime mosaic_runtime;

mosaic_runtime *mosaic_runtime_open_many(const char *const *paths, size_t n_packs, char *errbuf, size_t errlen);
mosaic_runtime *mosaic_runtime_open(const char *pack_path, char *errbuf, size_t errlen);
/* M4-3:世界内动态加载——向已打开的实例追加一个 pack(零重启)。校验纪律与
   open_many 单 pack 一致:格式校验、事件表与 pack 0 完全一致、模块范围与既有
   packs 不重叠(模块 id 全局唯一,fn_id 空间无冲突)。成功 0;失败 -1 + errbuf
   (文案复用 "event table mismatch" / "overlapping pack module ranges")。
   挂载后既有 find_function / 事件派发自动覆盖新 pack(dispatch 遍历
   rt->packs,无需其他改动);失败路径完全回滚(新 pack 的 fd/map 释放,
   rt 状态不变,function_count 不变)。 */
int mosaic_runtime_add_pack(mosaic_runtime *rt, const char *path, char *errbuf, size_t errlen);
void mosaic_runtime_close(mosaic_runtime *rt);
u32 mosaic_runtime_last_error(const mosaic_runtime *rt);
/* M4-3:当前已挂载 pack 数(status 观测用) */
u32 mosaic_runtime_pack_count(const mosaic_runtime *rt);
u64 mosaic_runtime_function_count(const mosaic_runtime *rt);
/* 当前工作集大小(已物化 ACTIVE 函数数)——M3-3 与驱逐调优用 */
u32 mosaic_runtime_working_set_count(const mosaic_runtime *rt);
const mosaic_module_record *mosaic_runtime_find_module(mosaic_runtime *rt, u64 module_id);
const mosaic_function_record *mosaic_runtime_find_function(mosaic_runtime *rt, u64 fn_id);
const char *mosaic_runtime_module_string(const mosaic_runtime *rt, const mosaic_module_record *m, u32 off);
u32 mosaic_runtime_event_id(mosaic_runtime *rt, const char *name);
#endif
