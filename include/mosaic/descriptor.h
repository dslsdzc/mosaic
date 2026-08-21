#ifndef MOSAIC_DESCRIPTOR_H
#define MOSAIC_DESCRIPTOR_H
#include "mosaic/base.h"
#include "mosaic/pack.h"
#include "mosaic/runtime.h"

/* M2-4:创造模式 Item 描述符查询(纯冷态)。
 *
 * 本头文件的所有查询只读 mmap 上的 item 记录表(v3),返回指向 mmap 内记录的
 * 描述符指针——不触发 dlopen、不物化 Runtime Object(设计规格第 8/9/12 节:
 * 浏览 Item 返回 Descriptor,只有把 provider fn_id 交给物化接口
 * (mosaic_fn_materialize)才真正加载模块。多 pack 下字符串读取用 uintptr
 * 扫描定位归属 pack(与 mosaic_runtime_module_string 同款模式)。
 *
 * 排序与查询语义:
 * - 每 pack 的 item 表按 (category, name) 升序,名字用长度感知比较
 *   (memcmp 前缀,相等比长度)——"sword"/"sword_iron" 前缀对各自精确命中,
 *   无 strcmp 式前缀误匹配;分类内名字互异(构建期拒绝重名)。
 * - by_name = 定位分类区间(二分 category 下界/上界)后按名字二分;
 *   for_each = 分类区间线性扫。跨 pack 自动合并:名字空间全局,pack 顺序
 *   (范围表序)即枚举顺序,by_name 返回首个命中的 pack 记录。
 * - 查询失败(miss)写 rt->last_err = MOSAIC_ERR_NOT_FOUND(与 find_module/
 *   find_function 同惯例)。 */

u64 mosaic_item_count(mosaic_runtime *rt);
const mosaic_item_record *mosaic_item_by_name(mosaic_runtime *rt, u32 category, const char *name);
int mosaic_item_for_each(mosaic_runtime *rt, u32 category,
                         int (*cb)(const mosaic_item_record *item, void *user), void *user);
const char *mosaic_item_name(mosaic_runtime *rt, const mosaic_item_record *item);
const char *mosaic_item_tags(mosaic_runtime *rt, const mosaic_item_record *item);
const char *mosaic_item_icon(mosaic_runtime *rt, const mosaic_item_record *item);
#endif
