#ifndef MOSAIC_DEPS_H
#define MOSAIC_DEPS_H
#include "mosaic/base.h"
#include "mosaic/runtime.h"

/* M2-1:依赖图遍历与闭包解析(M2 事务 validate 的依赖闭包前置能力)。
 *
 * 依赖表(mosaic_dep_entry,16B:(owner_id, dep_id))按 owner 排序,存储于
 * 所属 pack 内;模块记录的 dep_off 指向本 pack 依赖表首条依赖下标(无依赖 =
 * MOSAIC_DEP_NONE)。dep_id 是全局 module_id,可指向其他 pack 的模块——
 * 跨 pack 依赖合法,解析在运行时合并视图(open_many)上进行。 */

/* 遍历 module_id 的直接依赖:对每条依赖调用 cb(dep_module_id, user)。
   返回 0 = 正常结束(回调返回 0 继续);回调返回非 0 时停止遍历,
   for_each 透传该值返回。模块不存在 → 返回 -1,rt->last_err = NOT_FOUND。 */
typedef int (*mosaic_dep_cb)(u64 dep_module_id, void *user);
int mosaic_module_for_each_dep(mosaic_runtime *rt, u64 module_id, mosaic_dep_cb cb, void *user);

/* 版本约束:min/max 任一为 0 = 该端无界。 */
typedef struct { u32 min_version; u32 max_version; } mosaic_version_constraint;

/* 解析 module_id 的依赖闭包(含自身):深度优先 + 拓扑序输出
   (依赖先于依赖者;闭包内任意合法序)。
   - self_constraint 约束入口模块自身(NULL = 无约束);违反 → -1 + ABI。
   - M1 依赖条目 (owner, dep) 没有版本约束字段 → 依赖版本一律接受(无界),
     完整版本约束支持依赖 v3 格式为条目预留字段;解析器只检查存在性与环:
     缺失模块 → -1 + NOT_FOUND;环 → -1 + ILLEGAL;容量不足 → -1 + NOMEM。
   - 探测模式:out_cap == 0 只把所需长度写入 out_len 并返回 0(两阶段调用:
     先探测后填充);容量足够时写入 out[0..out_len) 并返回 0。out_len 可 NULL。 */
int mosaic_dep_resolve(mosaic_runtime *rt, u64 module_id, const mosaic_version_constraint *self_constraint,
                       u64 *out, size_t out_cap, size_t *out_len);
#endif
