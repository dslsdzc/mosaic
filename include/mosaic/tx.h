#ifndef MOSAIC_TX_H
#define MOSAIC_TX_H
#include "mosaic/runtime.h"

/* M2-2b:补丁 pack 事务 API(设计规格第 7 节"事务 / 滚动更新"完整落地)。
 *
 * 生命周期:begin → validate → commit(或 begin → abort)。
 * - begin:    mmap 打开补丁 pack 并做 begin 级校验(魔数/版本/偏移、事件表与
 *             base pack 0 完全一致、补丁模块/函数必须存在于 base、补丁内同
 *             fn_id 单条、generation 必须比 base 当前活跃代新、模块版本不回退)。
 *             任何失败 → 释放全部、返回 NULL + errbuf(无运行时副作用)。
 * - validate: 依赖闭包(M2-1 语义)、ABI 只读探测(不进入 runtime mods 表)、
 *             transform 索引越界、code_off 越界。全部通过才可 commit;失败
 *             → -1 + errbuf,可 abort(探测的 .so 缓存于 tx,commit 复用)。
 * - commit:   单线程冻结语义。构建新 gen_route 表(当前路由副本 + 补丁条目)→
 *             状态迁移(v1 → v2,transform 钩子,结果写补丁 pack blob)→
 *             quiesce v1(live 对象墓碑,保 v1 状态供 demote)→ 原子切换路由
 *             → 补丁 pack 转 runtime 持有(rt->tx_packs)。任一步失败 →
 *             -1 + errbuf 且路由已还原、补丁不转持(tx 可 abort)。
 * - rollback: demote——路由切回旧表、补丁 pack 从 rt->tx_packs 移除并 unmap
 *             (记录在磁盘仍在,重开可再 begin)。
 * - abort:    未 commit 的 tx 释放补丁 mmap + 探测 so/abi(dlclose),无运行时
 *             副作用。free 是唯一释放入口:abort 不释放句柄。
 *
 * 混合版本共存:generation 是函数级字段,路由只覆盖补丁函数,其余函数天然走
 * v1(设计规格:foo() 走 v2、bar() 走 v1 同时成立)。回滚 = demote(Myedsua
 * 模式):v1 state 从未销毁(墓碑序列化过),恢复成本 = 反序列化。 */
typedef struct mosaic_tx mosaic_tx;

mosaic_tx *mosaic_tx_begin(mosaic_runtime *rt, const char *tx_pack_path, char *errbuf, size_t errlen);
int mosaic_tx_validate(mosaic_tx *tx, char *errbuf, size_t errlen);
int mosaic_tx_commit(mosaic_tx *tx, char *errbuf, size_t errlen);
int mosaic_tx_rollback(mosaic_tx *tx, char *errbuf, size_t errlen);
void mosaic_tx_abort(mosaic_tx *tx);
void mosaic_tx_free(mosaic_tx *tx);
#endif
