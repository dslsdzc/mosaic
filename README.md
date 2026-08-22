# Mosaic

面向 Minecraft 的 **Native、事件驱动、函数级惰性模块运行时 —— 一个自研的模组加载器**。

> 巨大的 Mod Universe + 极小的 Runtime Working Set:千万级函数保存在紧凑的 mmap 静态索引中,只在逻辑真正被触发时自动物化对应函数;世界运行中可安装新 Mod、函数级滚动更新,全程零重启。

**Mosaic 自己就是加载器**:不基于 Paper/Fabric/Forge 任何现有加载器。核心为纯 C 运行时(C11,零外部依赖),经自研 JVM Bridge 与自研 javaagent 注入引擎接入 vanilla Minecraft 服务端(jar 零修改)。

## 里程碑状态

| 里程碑 | 内容 | 状态 |
|---|---|---|
| **M1** 核心循环 | mmap 冷存储(48B/64B 记录)、紧凑索引、工作集/状态机(物化/墓碑/恢复)、触发索引派发、所有权/驱逐 | ✅ 10M 冷函数 RSS 增量 0.12MB、全循环 ~200μs、热路径 ~1.00× |
| **M1.5** 分片 | 多 pack 合并索引(open_many)、模块范围表两级二分、按 pack mremap | ✅ 1 亿函数 = 100 片 × 100 万,构建峰值 83.9MB/片,打开 RSS 61.65MB |
| **M2** 事务/调度 | 依赖图闭包、事务 API(prepare/validate/commit/rollback=demote/abort)、函数级 generation 路由、状态迁移钩子(state_transform)、混合版本共存、DAG 调度器(线程池/优先级/亲和性/取消) | ✅ 跨两次 commit 状态链 3→30→32→320→322 逐步可复核;1M 派发 1109.7s→4.0s(ws 键位混合) |
| **M3** 合成世界 | 事件类型 API v1(205 事件目录 + 载荷签名 + 频率档)、合成世界模拟器、世界场景门禁 | ✅ 生命周期全循环 58.7μs;稀疏订阅工作集 28,149/100,000(≪ 总数) |
| **M4** 真实 MC | 自研 javaagent 注入(ClassFileTransformer + 自研 hook 点)、vanilla 1.20.1 集成、世界内动态加载 | ✅ 服务端 tick→注入→bridge→C 派发活循环;运行中 install 新 pack 下个 tick 即生效,零重启 |
| **M5** 兼容层 | Fabric/NeoForge mod 兼容 Provider | ⏳ 暂缓 |

## 设计

- 规格:`docs/superpowers/specs/2026-08-21-mosaic-runtime-design.md`
- 实现计划:`docs/superpowers/plans/2026-08-21-mosaic-m1-core.md`

## 构建与验证

```bash
./ci/gates.sh          # Release 构建 + 全部测试 + 10M 基准门禁 + 100M 分片门禁 + 世界场景门禁
bash ci/run_jni_test.sh  # JVM Bridge 测试(需 JDK 21)
```

## 核心指标(CI 门禁,实测为多轮稳定代表值)

| 门禁 | 指标 | 实测 | 阈值 |
|---|---|---|---|
| S1 冷规模 | 10M 冷函数 RSS 增量 | 0.12 MB | ≤ 80 MB |
| S3 全循环 | 触发→物化→执行→墓碑→恢复→执行 | ~0.2 ms | ≤ 500 μs |
| S4 热路径 | 分派/直调比 | ~1.00 | ≤ 1.10 |
| S5 分片 | 100 片 × 100 万构建峰值/打开 RSS | 83.9 MB / 61.65 MB | ≤ 300 MB / ≤ 80 MB |
| S-W1 世界生命周期 | 加入→物化→墓碑→重进恢复 | 58.7 μs | ≤ 500 μs |
| S-W2b 稀疏工作集 | 2% 高频订阅下 ws/总数 | 0.281 | ≪ 1 |

测试:16 个 C 套件(单元/属性/事务/线程/描述符/世界)+ JNI 23 断言 + 真实 1.20.1 服务端端到端;ASan/UBSan/TSan 干净。

## 布局

```
include/mosaic/  稳定 C API 头(base/pack/runtime/module/function/event/ownership/eviction/deps/tx/sched/world/descriptor/events)
src/             pack_builder·runtime·index·working_set·lifecycle·trigger·ownership·eviction·deps·tx·genroute·sched·world·descriptor·events
src/jni/         JVM Bridge(JNI 双向通道,M4-1)
java/mosaic/     Java 稳定 API 面(mosaic.Bridge)
agent/           自研注入引擎(javaagent + ClassFileTransformer + hooks,M4-2)
bench/           合成宇宙 + S1-S5 基准 + 世界场景 + pack 生成器
tests/           mini_test 单元/属性测试
ci/              gates.sh·setup_mc_server.sh·build_mc_agent.sh·run_jni_test.sh
```

## 架构

```
Minecraft 服务端 (vanilla 1.20.1,jar 零修改)
    ↕ 自研 javaagent 注入(ClassFileTransformer + 自研 hook 点)
JVM Bridge (JNI,载荷小端 byte[] ↔ events.h 结构体)
    ↕ Stable Runtime ABI
C Runtime Core (mmap 冷存储 / 紧凑索引 / 工作集 / 状态机 / 触发索引 / 事务 / 调度)
    ↕ 模块 ABI v2 (dlopen,code 表 + state_transform 表)
Mod Universe (pack 文件:记录 48B/64B/16B,分片可挂载)
```

事件类型 API:`include/mosaic/events.h` 205 事件目录(域分类 + 载荷签名 + 频率档),`MOSAIC_MAX_EVENTS 4096`。

## JVM Bridge(M4-1)

设计规格第 24 节:Java(Minecraft 侧)经 Minimal Bridge 接入 C 运行时,
零 MC 依赖、纯本地可测。Java 侧 API 面 = `mosaic.Bridge`
(`java/mosaic/Bridge.java`):`runtimeOpen/Close`(pack 组打开/关闭)、
`functionCount`、`eventId`(名 → id,未注册 -1)、`eventDispatch`
(byte[] 载荷 → 执行数)、`workingSetCount`、`lastError`、
`runtimeAddPack`(世界内挂载)。载荷约定:
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
ProGuard 内联,**控制台/RCON 命令**统一漏斗 = `performPrefixedCommand`;
游戏内聊天命令暂未挂钩——独立反汇编证实聊天命令不走该漏斗(走
`dt.a(ParseResults,String)`,即 mojmap `performCommand(ParseResults,String)`
路径),需 hook 该点,留待后续。)

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

## 世界内动态加载(M4-3)

**服务器运行中安装新 pack(新 Mod),零重启**:C 运行时
`mosaic_runtime_add_pack(rt, path, errbuf, errlen)`(校验纪律与 open_many
单 pack 一致:格式校验 → 事件表与 pack 0 一致 → 模块范围不重叠;失败完全
回滚,function_count/pack_count 不变),JNI `Bridge.runtimeAddPack`,
agent 命令 `/mosaic install <pack路径>`。挂载后既有派发自动覆盖新 pack
(dispatch 遍历 `rt->packs`),**下个 tick 即执行其 tick 订阅者**——无需
`/reload`、无需重启服务端。

```bash
build/gen_world_pack mc-server/packs/world2.pack "$PWD/build/libtest_mod.so" world2
# 控制台(服务端运行中):
#   /mosaic status                       → functions=13 packs=1 ...
#   /mosaic install <绝对路径>/world2.pack → installed ... (functions=13->19)
#   /mosaic status                       → functions=19 packs=2 ...
#   /mosaic install <world.pack 重叠>     → install ... failed (err=1)
```

端到端验证证据(`mc-server/console.log`):安装前 tick 每 tick 执行
`executed/calls = 3.0`(world 的 3 个 tick 订阅者);安装 world2 后 10 秒
窗口 `calls 456→661`(205 ticks)、`executed 1974→3204`(1230 次)→
**每 tick 6.0 = world 3 + world2 3**,world2 的 tick 订阅者随安装后下个
tick 立即执行(世界内加载生效);重叠安装 world.pack(模块 1 与已挂载
模块 1 重叠)→ `failed (err=1)`。JNI 侧 `ci/run_jni_test.sh` 追加断言:
`runtimeAddPack` 成功(函数数 3→5、派发 2→3)、重叠失败 -1 + `lastError`
非 0、失败后函数数不变;`test_shards` 新增 4 个用例(挂载/重叠回滚/事件
不一致/重复挂载)。

## 已知边界

- 1.20.1 服务端运行需接受 Mojang EULA(`mc-server/eula.txt` → `eula=true`,本地测试已接受)。
- 游戏内聊天命令暂未挂钩(仅控制台/RCON 命令漏斗)。
- `mosaic_runtime_add_pack` 非线程安全(单线程服务端线程前提)。
- M4 阶段 API 尚未覆盖原版能力域(entity/block/item/registry 等)——见 `docs/` 与规划中的 API 大类清单。
