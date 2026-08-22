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
src/jni/         JVM Bridge(JNI 双向通道,M4-1)
java/mosaic/     Java 稳定 API 面(mosaic.Bridge)
bench/           合成宇宙 + S1-S4 基准 + world 场景 + gen_test_pack
tests/           mini_test 单元/属性测试
ci/gates.sh      验收门禁
```

## JVM Bridge(M4-1)

设计规格第 24 节:Java(Minecraft 侧)经 Minimal Bridge 接入 C 运行时,
零 MC 依赖、纯本地可测。Java 侧 API 面 = `mosaic.Bridge`
(`java/mosaic/Bridge.java`):`runtimeOpen/Close`(pack 组打开/关闭)、
`functionCount`、`eventId`(名 → id,未注册 -1)、`eventDispatch`
(byte[] 载荷 → 执行数)、`workingSetCount`、`lastError`。载荷约定:
`byte[]` 与 `include/mosaic/events.h` 载荷结构体**小端一致**,长度 =
结构体大小(例:方块事件 16B = player_id/x/y/z/block_type;玩家事件 4B)。

构建与运行 JNI 测试(需要 JDK 21;JAVA_HOME 未设时用 `/usr/lib/jvm/default`):

```bash
bash ci/run_jni_test.sh      # cmake build → gen_test_pack → javac → java 断言,exit 0
```

产物:`build/lib/libmosaic_jni.so`(CMake FindJNI + mosaic_core 静态库链接)。
