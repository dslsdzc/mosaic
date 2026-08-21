# Mosaic

面向 Minecraft 的 Native、事件驱动、函数级惰性模块运行时(M1:核心循环原型)。

## 设计

- 规格:`docs/superpowers/specs/2026-08-21-mosaic-runtime-design.md`
- 实现计划:`docs/superpowers/plans/2026-08-21-mosaic-m1-core.md`

## 构建与验证

```bash
./ci/gates.sh          # Release 构建 + 全部测试 + 10M 基准门禁
```

## M1 验收硬指标(CI 门禁)

| 门禁 | 指标 | 实测 | 阈值 |
|---|---|---|---|
| S1 冷规模 | 10M 冷函数 RSS 增量 | 0.12 MB | ≤ 80 MB |
| S3 全循环 | 触发→物化→执行→墓碑→恢复→执行 | ~0.3 ms | ≤ 500 μs |
| S4 热路径 | 分派/直调比 | ~1.00 | ≤ 1.10 |
| S2 冷启动 | 1k 函数物化延迟 | — | 诊断性,非门禁 |

实测值为代表值(中位数决策,多轮稳定);偶发机器负载波动被中位数设计压制,单轮偏差不判失败。

## 布局

```
include/mosaic/  稳定 API 头(base/pack/runtime/module/function/event/ownership/eviction)
src/             pack_reader·index·working_set·lifecycle(L0-L3)·trigger·ownership·eviction(L4)
src/pack_builder.c   离线 pack 构建器
bench/           合成宇宙 + S1-S4 基准
tests/           mini_test 单元/属性测试
ci/gates.sh      验收门禁
```
