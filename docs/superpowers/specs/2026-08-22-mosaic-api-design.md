# Mosaic API 面设计

> 日期:2026-08-22
> 状态:已确认(经头脑风暴逐节评审)

## 1. 定位与目标

Mosaic 的稳定 API 面:Mod 作者编写逻辑所依赖的、**只增不减、跨 Minecraft 大版本稳定**的 Java API。兼容层(Fabric/NeoForge mod 兼容)暂缓;本设计专注 Mosaic 自身 API 面。

**第一原则(用户确认):**

1. **只增不减** —— API 显式版本化;新增走新方法/新类型,旧签名永不删除、永不改语义;描述符/载荷/pack 格式同规则(字段只加不改)。
2. **跨大版本稳定** —— 抽象面独立于原版实现细节;1.12→1.13 扁平化、1.20.5+ DataComponent 等大改由 Provider 层吸收,API 本身不破坏。

## 2. 已确认的关键决策

| 决策点 | 结论 |
|---|---|
| 版本参照 | **域模型双代锚定**:语义以 1.8.9(MCP 逆向,`~/minecraft1.8.9`)∩ 26.2(官方逆向,`~/minecraft26.2`)共同能力为界;首发 Provider 26.2,1.8.9 Provider 同步规划作跨大版本实证;1.20.1 Provider 后补 |
| API 形态 | **Java 为唯一稳定 API 面**;C API 退为内核(不对外承诺);运行时域实现经 JNI 到 C 核心,原版域实现 = Provider 纯 Java 转换 |
| 版本机制 | **版本常量 + 区间校验 + since 标注 + 兼容测试套件**(只增不减的机器保证) |
| 原版域形态 | **句柄式域模型**(方案 A):每域少量核心句柄接口 + 语义操作,原版能力的稳定投影,不镜像类层级 |
| 排除 | 不集成 NeoForge 自创系统(energy/fluids/transfer/attachment——原版没有);M5 兼容层暂缓 |
| 命名 | `Mosaic` + 驼峰单词,无下划线、无缩写;与 C 侧 `mosaic_*` 蛇形一一对应 |
| 规模 | 31 大类(21 运行时 + 10 原版域),约 80 接口 + 辅助 ≈ 120-150 类型 |

## 3. 总体架构

```
┌─────────────────────────────────────────────────┐
│ Mod 作者                                        │
├─────────────────────────────────────────────────┤
│ 域模型接口层(Java,唯一稳定面,只增不减)           │
│   runtime 域: MosaicRuntime/MosaicEvent/…(21 类) │
│   vanilla 域: MosaicBlock/MosaicItem/…(10 类)    │
│   版本: API_VERSION 常量 + since 标注             │
├─────────────────────────────────────────────────┤
│ 实现层(不承诺稳定,可自由演进)                    │
│   运行时域: JNI → C 核心(现有 mosaic_* 内核)     │
│   原版域:   Provider 转换器(版本差异全吸收)       │
├─────────────────────────────────────────────────┤
│ 版本适配                                        │
│   Provider 26.2(首发)· Provider 1.8.9(实证)     │
│   Provider 1.20.1(M4 已集成,后补)               │
└─────────────────────────────────────────────────┘
```

**核心机制:**

1. **域模型 = 原版能力的稳定投影**:`MosaicBlock` 不是原版 `Block` 的别名,是"方块"语义的稳定接口——26.2 和 1.8.9 的 Block 都转换成同一个 `MosaicBlock`。数字 ID / 注册表 ID / DataComponent 的差异只存在于 Provider 内部。
2. **双代锚定**:域模型接口的语义以"1.8.9 ∩ 26.2 共同能力"为界;只有一代有的能力(如 26.2 的 component)用 since 标注渐进加入(只增不减)。
3. **实现分离**:运行时域接口的 JNI 实现与 Provider 无关;原版域接口的 Provider 实现与 C 核心无关(纯 Java 转换)。
4. **兼容性测试是机制的一部分**:`compat/` 下用早期 API 写的样例 mod,每次变更跑一遍编译 + 运行——只增不减靠测试锁定。

## 4. 运行时域接口清单(21 类)

| 域 | 句柄接口 | 核心语义操作 |
|---|---|---|
| Runtime | `MosaicRuntime` | open/close/addPack/workingSetCount/lastError |
| Pack | `MosaicPackBuilder` `MosaicPackInfo` | 构建/读取/校验(pack v3 格式) |
| Index | `MosaicIndexQuery` `MosaicFunctionIndex` `MosaicModuleIndex` `MosaicEventIndex` `MosaicItemIndex` | 二分查询,纯冷态 |
| Descriptor | `MosaicFunctionDescriptor` `MosaicModuleDescriptor` `MosaicItemDescriptor` `MosaicEventDescriptor` | 冷态描述,零物化 |
| Lifecycle | `MosaicFunctionLifecycle` | materialize/tombstone/restore |
| Activation | `MosaicActivationPolicy` `MosaicActivationGate` | 惰性门控 |
| Eviction | `MosaicEvictionPolicy` `MosaicWorkingSet` `MosaicWorkingSetStats` | 窗口驱逐 |
| State | `MosaicFunctionState` `MosaicStateStore` `MosaicStateTransform` | 状态读写/迁移 |
| Module | `MosaicModule` `MosaicModuleContext` `MosaicModuleInfo` `MosaicModuleLoader` | 模块装载/统一资源归属 |
| Dependency | `MosaicDependencyGraph` `MosaicVersionConstraint` `MosaicDependencyResolver` | 闭包/环检测/约束 |
| Transaction | `MosaicTransaction` `MosaicTxPatch` `MosaicTxResult` | prepare/validate/commit/rollback/abort |
| Event | `MosaicEvent` `MosaicEventDispatcher` `MosaicEventSubscription` `MosaicEventCatalog` `MosaicEventPayload` | 派发/订阅/目录(205 事件) |
| Trigger | `MosaicTriggerIndex` `MosaicTriggerEntry` | 触发索引 |
| Scheduler | `MosaicScheduler` `MosaicSchedulerConfig` | 线程池/DAG |
| Task | `MosaicTask` `MosaicTaskDependency` `MosaicTaskPriority` `MosaicTaskResult` `MosaicCheckpoint` | 任务图/取消 |
| Lease | `MosaicLease` `MosaicOwnedResource` `MosaicResourceHandle` | 租约/所有权 |
| Resource | `MosaicResourceManager` `MosaicResourceLease` | 资源生命周期 |
| Service | `MosaicService` `MosaicServiceRegistry` `MosaicServiceRef` | 服务注册/发现 |
| Capability | `MosaicCapability` `MosaicCapabilityProvider` `MosaicCapabilityQuery` | 能力提供 |
| Query | `MosaicQuery` `MosaicQueryBuilder` `MosaicQueryResult` | 创造模式查询(浏览不物化) |
| Bridge | `MosaicBridge` `MosaicBridgeException` `MosaicPayloadCodec` | Java↔C 通道 |

## 5. 原版域接口清单(10 类,句柄式稳定投影)

| 域 | 句柄接口 | 语义操作 | 双代锚定(逆向对照) |
|---|---|---|---|
| World | `MosaicWorld` | 维度/方块读/实体遍历/世界推进(tick、保存并入) | 1.8.9 `world.World` ↔ 26.2 `world.level.Level` |
| Block | `MosaicBlock` `MosaicBlockState` `MosaicBlockPos` | 状态/坐标/属性集 | `block.Block` ↔ `world.level.block.Block`(ID 体系差异在 Provider) |
| Item | `MosaicItem` `MosaicItemStack` `MosaicComponents` | 栈/组件集(26.2 起,since 标注) | `item.Item` ↔ `world.item.Item`(1.8.9 无组件→空实现) |
| Inventory | `MosaicInventory` `MosaicInventorySlot` | 槽位/计数/移动 | `inventory.*` ↔ `world.inventory.*` |
| Entity | `MosaicEntity` `MosaicEntityId` `MosaicEntityType` | id/类型/位置/属性 | `entity.Entity` ↔ `world.entity.Entity` |
| Player | `MosaicPlayer` `MosaicPlayerSession` | 会话/游戏模式/加入离开 | `server.players.PlayerList` ↔ `server.players.PlayerList` |
| Registry | `MosaicRegistry` `MosaicRegistryEntry` | id↔名双向映射(差异全在 Provider) | 1.8.9 无注册表→Provider 合成 |
| Command | `MosaicCommand` `MosaicCommandTree` | 命令注册/执行 | 1.8.9 `command.*` ↔ 26.2 Brigadier `commands.*` |
| Network | `MosaicNetwork` `MosaicPacketListener` | 包收发/监听器 | `network.*` ↔ `network.protocol.*` |
| Nbt | `MosaicNbt` `MosaicNbtCompound` | 复合标签读写 | `nbt.*` ↔ `nbt.*`(两代语义最稳定) |

**双代锚定规则**:每个接口的语义以"1.8.9 ∩ 26.2 共同能力"为基准定义;只有单代的能力(since 标注);Provider 实现保证同一接口在两代下行为一致(对照测试)。

## 6. Provider 机制

```java
public interface MosaicProvider {
    String providerId();            // "vanilla-26.2" / "vanilla-1.8.9"
    String mcVersion();             // "26.2" / "1.8.9"
    boolean supportsApi(int min, int max);   // 该 Provider 实现的 API 版本区间
    MosaicBlock blockOf(Object vanillaBlock);
    MosaicItem itemOf(Object vanillaItem);
    MosaicWorld worldOf(Object vanillaWorld);
    // ... 各域句柄工厂
}
```

- **注册与选择**:加载期按当前 MC 版本选 Provider;无匹配 Provider → 拒绝加载,错误信息指明"需要 version X 的 Provider"。
- **句柄持有原版引用**:`MosaicBlock` 实现内部持 26.2 的 `world.level.block.Block` 或 1.8.9 的 `block.Block`,接口方法读取时经 Provider 转换——转换只在读路径;写路径(操作原版)由 Provider 定义能力边界。
- **双代对照测试**:域模型契约测试与版本无关(同一套用例),26.2 与 1.8.9 的类不能同 JVM——对照 = 同一套契约测试在两个 Provider 环境分别跑(CI 分两个测试任务,共享测试源码),断言同一域模型行为一致。
- 1.8.9 Provider 职责边界:域模型只要求"共同能力"一致;1.8.9 没有的能力(component)在 1.8.9 环境下返回空/不可用语义(契约测试里标 since 的用例在该版本跳过)。

## 7. 版本机制(只增不减的落地)

| 机制 | 形态 |
|---|---|
| API_VERSION | `MosaicApi.API_VERSION` 常量(当前 1) |
| mod 声明 | `mosaic.api-required = [min, max]`(mod 元数据) |
| 加载器校验 | `max > API_VERSION` → 拒绝加载 + 明确提示 |
| since 标注 | `@Since(2)` 注解(可反射校验;`MosaicApi.requireApi(2)` 运行时守卫) |
| 兼容测试套件 | `compat/` 下用 v1 API 写的样例 mod——每次 API 变更跑编译 + 运行;**v1 签名被删/改 → 编译失败 → CI 门禁红** |

- **只增不减的机器保证**:兼容套件编译失败即门禁失败——"减"在机制上不可能合入;语义变更靠契约测试(旧语义断言不变)。
- **跨版本承诺**:域模型接口的语义定义在文档(每接口一条"语义契约"),契约测试锁定;原版大改只改 Provider,不改接口。

## 8. 错误处理

```
MosaicApiException (基类)
 ├── MosaicHandleException       句柄失效(墓碑/卸载后访问)
 ├── MosaicProviderNotFoundException  当前 MC 版本无 Provider
 └── MosaicApiVersionException   mod 所需版本 > API_VERSION
```

事件载荷保持现有 byte[] ↔ events.h 约定;域模型之上提供 `MosaicEventPayload` 类型化包装(按事件域解码,解码失败 → MosaicHandleException 系)。

## 9. 交付拆分

| 子任务 | 内容 | 验证 |
|---|---|---|
| **M5-1 API 骨架** | `MosaicApi.API_VERSION`、异常层次、`@Since`、运行时域 21 类全部接口定义(纯接口零实现) | 接口编译 + 契约测试骨架(未实现 → 红) |
| **M5-2 运行时域实现** | JNI 实现(扩展现有 Bridge,runtime 域接口逐个接通 C 内核) | 契约测试全绿(无 MC 依赖) |
| **M5-3 原版域接口 + 26.2 Provider** | 10 域接口定义 + Provider 26.2 实现(基于逆向源码) | 契约测试在 26.2 环境全绿 |
| **M5-4 1.8.9 Provider + 双代对照** | Provider 1.8.9 实现(MCP 逆向)+ 同一套契约测试在 1.8.9 环境跑 | **双代对照:同套用例两环境全绿** |
| **M5-5 兼容套件 + 版本校验** | `compat/` v1 样例 mod + 加载器区间校验 + CI 集成 | 样例编译/运行绿;超版本 mod 被拒 |

## 10. 验证体系

1. **契约测试(域模型语义锁定)**:`tests/api-contract/` 版本无关用例——运行时域(物化/墓碑/派发语义)+ 原版域(句柄语义),跑在无 MC 环境(M5-2)与两代 Provider 环境(M5-3/4)。
2. **双代对照**:同一套原版域契约用例,26.2 与 1.8.9 各跑一遍(CI 两个任务共享测试源码)——跨大版本稳定性的实证门禁。
3. **兼容套件(只增不减的机器保证)**:`compat/` 用 v1 API 写的样例,每次 API 变更编译 + 运行;编译失败 = 门禁红。
4. **版本校验**:构造声明 `api-required = [1, 99]` 的 mod → 加载器拒绝 + 错误信息。
5. **回归**:既有 16 C 套件 + JNI 23 断言 + 真实服务端端到端全部保留。

## 11. 边界与取舍

- 原版域的"能力缺口"(equipment/variant 等)走 since 标注渐进补,首版不阻塞。
- C API 退为内核,不再对外承诺稳定(文档标注)。
- 1.20.1 Provider 后补(需先做 1.20.1 逆向或借 mappings;M4 集成目标与其 Provider 错位——26.2 首发是因为逆向现成)。
- 命名空间:接口 `mosaic.*` 包;运行时域 `mosaic.runtime.*`、原版域 `mosaic.vanilla.*`(或 `mosaic.world.*` 等按域分包,实施时定)。
