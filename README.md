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
结构体大小(例:方块事件 20B = player_id/x/y/z/block_type;玩家事件 4B)。

构建与运行 JNI 测试(需要 JDK 21;JAVA_HOME 未设时用 `/usr/lib/jvm/default`):

```bash
bash ci/run_jni_test.sh      # cmake build → gen_test_pack → javac → java 断言,exit 0
```

产物:`build/lib/libmosaic_jni.so`(CMake FindJNI + mosaic_core 静态库链接)。

## 1.20.1 服务端集成(M4-2)

**Mosaic 自己就是模组加载器**:不基于 Paper/Fabric/Forge 任何现有加载器。
注入机制 = **JVM 标准 Instrumentation API**(`-javaagent` + 自研
`ClassFileTransformer`),vanilla 1.20.1 服务端 jar **零修改**;ASM 9.7 仅作
字节码编辑工具库(单一核心 jar,类并入 agent),注入引擎/hook 点/事件映射
全部自研。注意:官方 1.20.1 服务端 jar 经 **ProGuard 混淆**(Mojang 自
1.20.1 起),mojmap 名运行时不存在——hook 点用 Mojang `server_mappings`
+ javap 逐项核实为混淆名(见 `agent/com/mosaic/agent/MosaicHooks.java`
头注释),事件名(世界 pack 与 hooks 派发名)与 `include/mosaic/events.h`
目录一致。

注入点(1.20.1 混淆签名):

| mojmap 方法 | 混淆名/签名 | hook |
|---|---|---|
| PlayerList.placeNewPlayer(Connection, ServerPlayer) | `alk.a (Lsd;Laig;)V` | `onPlayerJoin`(方法尾) |
| PlayerList.remove(ServerPlayer) | `alk.c (Laig;)V` | `onPlayerLeave`(方法尾) |
| ServerPlayerGameMode.destroyBlock(BlockPos) | `aih.a (Lgu;)Z` | `onBlockBreak`(入口,取破坏前状态) |
| Commands.performPrefixedCommand(CommandSourceStack, String) | `dt.a (Lds;Ljava/lang/String;)I` | `onCommand`(入口,消费 `/mosaic`) |
| MinecraftServer.tickServer(BooleanSupplier) | `MinecraftServer.a (Ljava/util/function/BooleanSupplier;)V` | `onServerTick`(方法尾,每 tick 派发) |

(1.20.1 中 mojmap 的 `performCommand(CommandSourceStack,String)` 已被
ProGuard 内联,控制台/聊天命令统一漏斗 = `performPrefixedCommand`。)

关键工程点:

- **bundler 类加载器**:1.20.1 服务端由 bundler 自建 URLClassLoader(父 =
  platform)加载,agent 类对它不可见——premain 用
  `Instrumentation.appendToBootstrapClassLoaderSearch(agentJar)` 挂到
  bootstrap 搜索路径,任何加载器经委托链都能解析 hooks 与 Bridge(单一实例,
  premain 初始化的静态状态即 hook 所见)。
- **原生库零依赖**:`libmosaic_jni.so` 并入 agent jar,premain 解出临时目录并
  `System.load`(`mosaic.jni.lib` 属性;JDK 的 `java.library.path` 在 premain
  前已缓存,setProperty 无效,故 agent 内嵌版 Bridge 静态块按绝对路径加载)。
- **帧重算**:COMPUTE_FRAMES 类加载期重算,公共父类用服务端定义加载器解析,
  失败回退 Object;任何转换异常 → 该类原样加载(注入不崩服务端)。
- **事件载荷**小端 LE,与 `events.h` 结构体一致(player 域 4B、block 域 20B)。

运行方式:

```bash
bash ci/setup_mc_server.sh      # 下载官方 server.jar + asm.jar、eula/properties、生成 mc-server/packs/world.pack
bash ci/build_mc_agent.sh       # javac + jar cfm → build/lib/mosaic-agent.jar(asm 类与 .so 并入)
cd mc-server && java -javaagent:../build/lib/mosaic-agent.jar -Xmx1G \
    -jar minecraft_server.1.20.1.jar nogui
# 控制台:/mosaic status(函数数/工作集/各事件派发计数)、/mosaic test <event> [payload...]
```

端到端验证证据(`mc-server/console.log`):`runtime opened (functions=13)`、
`transformed net/minecraft/server/MinecraftServer / dt / alk`、tick 派发计数
每 tick 增长(`calls=166 executed=498` → 6 秒后 `calls=298 executed=894`)、
`/mosaic test block_break 7 10 20 30 1` → `executed=3`。
