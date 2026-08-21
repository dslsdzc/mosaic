# Mosaic — Minecraft Mod Runtime 设计文档

> 日期:2026-08-21
> 状态:已确认(经头脑风暴逐节评审)

## 1. 项目定位

面向 Minecraft 的 **Native、事件驱动、函数级惰性模块运行时**:将庞大的 Mod Universe 保存在紧凑的静态索引中,仅在逻辑真正具有触发需求时自动物化对应函数,并通过 Working Set、Tombstone、Ownership 和 Transaction 机制实现世界内加载、自动回收和函数级滚动更新。

核心度量:**巨大的 Mod Universe + 极小的 Runtime Working Set**。

```
所有已安装 Mod / 函数 / 资源
        ↓
静态索引 / 冷态描述          ← mmap / 页缓存,不物化
        ↓
只有真正需要的逻辑            ← 事件驱动激活
        ↓
运行时物化                    ← 函数级粒度
        ↓
执行
        ↓
长期不用则降级/墓碑化          ← 工作集驱逐
        ↓
再次需要时恢复                ← 自动 Restore
```

## 2. 核心哲学(浓缩自规格 28 节)

1. **Lazy by default** — 默认不物化,只有语义上必须时才主动物化,不需要开发者显式标注。
2. **Event driven** — 逻辑存在不等于逻辑必须运行;只有实际触发路径时才激活。
3. **Function granularity** — 生命周期管理的最小粒度是函数/逻辑,而不是 Mod。
4. **Static information ≠ Runtime state** — Descriptor / Index / Manifest 不等于 Runtime Object。
5. **Installed size ≠ Runtime memory** — 千万级 Mod 不应意味着千万级 Runtime Objects。
6. **Working Set is what matters** — 内存由当前活跃工作集决定(Denning 工作集模型,已被证明近似最优且免疫 thrashing)。
7. **API can be huge, Core cannot** — API 面可以很宽(Forge 级),Core 保持最小。
8. **Ownership is fundamental** — 所有 Runtime Resource 有明确 Owner 和 Lifetime。
9. **Updates are transactions** — 滚动更新必须支持 prepare / validate / commit / rollback。
10. **Lazy is automatic** — 运行时自动进行 materialization / suspension / restoration。

## 3. 已确认的关键决策(头脑风暴结论)

| 决策点 | 结论 |
|---|---|
| 执行基板 | **原生 .so + 稳定 ABI**(Mod 编译为原生库,冷态仅元数据,物化 = dlopen + 符号解析 + 状态分配;Java Mod 后续经 JVM Bridge 通道) |
| 成功标准 | **激进硬指标**:10M 冷函数 RSS 增量 ≤ 80MB、全循环 ≤ 500μs、热路径零检查、基准进 CI |
| 集成形态 | **服务端优先**(世界内加载/事件/实体均为服务端逻辑;渲染/材质/UI 后置) |
| 目标 MC 版本 | **1.20.1**(2023,经典 modding 锚点,四生态全支持) |
| 冷层方案 | **自建紧凑 mmap 冷存储 + 双级索引**(方案 A;分列式/rank-select 路线作为演进方向保留) |
| 事务实现时机 | M2(M1 只留 generation 字段与格式支持,不参与验收指标) |
| 项目代号 | Mosaic |

## 4. 总体架构

```
┌─────────────────────────────────────────────────────┐
│ L5  API 面(稳定 C 头文件,runtime.h / module.h /      │
│     event.h / tx.h …;高聚合低耦合,不暴露内部结构)     │
├─────────────────────────────────────────────────────┤
│ L4  服务层:触发索引 · 所有权/租约 · 事务 · 调度器      │
├─────────────────────────────────────────────────────┤
│ L3  生命周期引擎:状态机 · resolve · quiesce · 墓碑    │
├─────────────────────────────────────────────────────┤
│ L2  工作集:arena 池 · 物化对象 · 热路径               │
├─────────────────────────────────────────────────────┤
│ L1  紧凑内存索引:ID→offset(微小,RAM 只放 KB 级)      │
├─────────────────────────────────────────────────────┤
│ L0  冷存储:mmap pack 文件(唯一事实源,页缓存承担)       │
└─────────────────────────────────────────────────────┘
```

### 四条架构不变量

1. **Installed ≠ Discovered ≠ Indexed ≠ Dormant ≠ Prepared ≠ Active** —— 冷数据只存在于 L0 的 mmap 里,任何层不准复制成对象图。
2. **冷存储是唯一事实源** —— 索引、工作集、墓碑记录都是派生视图;内存索引必须小到"偷不走页缓存"(文献:索引多用内存会挤占 OS 页缓存,增加缺页,反而拖慢端到端)。
3. **内存 ≈ 工作集** —— Denning 窗口 + 效用驱逐,与总安装量无关。
4. **所有权是根本** —— 任何物化资源必须有 Owner + 租约;卸载 = 停新工作 → drain → 释放 → 持久化。

## 5. 冷层设计 (L0/L1)

### 二进制布局原则

固定宽度、显式小端序、`static_assert` 尺寸校验;mmap 文件即数据库,不依赖结构体对齐,跨平台安全(大端机走 LE 访问层)。

### Pack 文件布局

```
┌──────────────────────────────────────────────┐
│ Pack Header (256B)  magic/version/各表偏移/计数 │
├──────────────────────────────────────────────┤
│ Module Table    ModuleRecord[N]  (64B each)   │
│ Function Table  FunctionRecord[M] (48B each)  │
│ Trigger Table   排序的 (event_id → fn 区间)     │
│ Dependency Table  (owner_id, dep_id) 对        │
│ State Blob      持久状态槽(墓碑时写入,可增长)    │
│ Metadata Blob   去重字符串/标签/清单(压缩,不热)  │
│ ID Index        排序 fn_id 数组 + tiny hash    │
└──────────────────────────────────────────────┘
```

### FunctionRecord(48B,核心冷记录)

```
u32 code_off     代码标识:模块代码表内偏移(dlopen 符号)
u32 dep_off      依赖表偏移
u32 state_off    状态槽偏移(0 = 无持久状态)
u32 meta_off     元数据偏移(名字/标签,冷,绝不热加载)
u64 id           全局唯一函数 ID
u32 module_id    归属模块
u16 trigger_off  触发表偏移(订阅了哪些事件)
u16 flags        位域:生命周期提示/语言/可否墓碑/需状态
u32 generation   版本代次(滚动更新 v1/v2 共存的关键)
u64 reserved     预留:权重/恢复成本提示(驱逐策略用)
```

### ID 方案与索引

- u64 id = `module_id << 32 | local_id`;module_id 在索引构建时分配。
- RAM 内只放:namespace→module_id 字符串哈希(KB 级)+ 每模块函数按 local_id 排序数组二分查找(mmap 内,走页缓存)。
- 刻意不做:RAM 对象图、记录复制、全量函数 ID 哈希。

### ≤8B/函数的指标语义

- 10M 函数 × 48B = 480MB **磁盘文件**——磁盘便宜,冷数据外置是设计本意。
- RSS 只算被触碰的页:基准只物化热集 + 索引头 → RSS 增量 ≈ KB~MB 级,摊派 ≈ 0B/函数。
- **指标写的是 RSS 增量,不是文件大小**——语义以此为准。

### 构建与扩展

- 并行构建器:按模块分片并行扫描/排序,顺序写出。
- 多 pack 分片:千万级宇宙可拆多个 pack(按模组分组),索引合并。
- 滚动更新预留:generation 字段 + 记录版本号;新版本写新 generation 记录,旧记录保留至事务切换。

## 6. 热层设计 (L2/L3)

### 核心决策:生命周期状态与驱逐优先级是两根正交的轴

生命周期状态描述"代码是否在运行路径上";驱逐优先级(WARM/COLD/COLDEST hint)描述"保持存活的效用"。驱逐决策只看效用,不绑状态——"温着但利用率低"与"冷着但恢复便宜"均可被独立决策(FaaShadow 2026 结论)。

### 状态机(5 态 + 2 瞬态)

```
        ┌────────────────────────────────────────────┐
        │          COLD(只有 48B 记录)                │
        │  (从未物化 或 已墓碑) ←────────────────────┐ │
        └───────┬────────────────────────────────────┘ │
        event   │ resolve+dlopen+state 分配            │ restore
                ▼                                       │
        ┌───────────────┐  派发    ┌───────────────┐    │
        │ MATERIALIZING │ ──────→ │    ACTIVE     │    │
        └───────────────┘ (瞬态)  │ 工作集中,可执行 │    │
                                  └───────┬───────┘    │
                       驱逐策略选中        │ 停新派发    │
                                  ┌───────▼───────┐    │
                                  │  QUIESCING    │────┘ (瞬态)
                                  │ drain refs→0  │  序列化 state
                                  └───────┬───────┘  释放 arena
                                          ▼         .so refcount--
                                       TOMBSTONED
```

状态只存于记录 flags 位域;工作集内才有 FnObj。

### 物化对象(~48B,arena 内)

```c
typedef struct {
  const FunctionRecord *rec;   // 回指 mmap 记录(唯一事实源)
  void (*code)(fn_ctx*, ...);  // 已解析代码指针(dlopen 符号)
  void *state;                 // arena 分配的运行时状态
  u32 refs;                    // 在途引用计数(quiesce 用它 drain)
  u64 last_use;                // Denning 窗口 T 追踪
  u64 util;                    // GDSF 权重:频率×恢复成本
  struct FnObj *next;          // 窗口链表
} FnObj;
```

### 三条路径

- **物化**(COLD→ACTIVE):触发索引命中 → resolve 模块 .so(引用计数,模块级 dlopen + 函数级符号缓存)→ arena 分配 state → 绑定 code → ACTIVE。基准测试用单合成 .so:10M 函数共享同一段代码、各有独立 state,验证完整真实路径。
- **热路径**:ACTIVE 函数 = 一次指针解引用,零状态机检查、零惰性开销;只有 COLD/TOMBSTONED 才付激活成本。
- **墓碑**(ACTIVE→QUIESCING→TOMBSTONED):停新派发 → drain(refs→0)→ 序列化 state 到 state blob → 释放 arena 块 → .so refcount-- → flags 置 TOMBSTONED(记录永不消失)。
- **恢复** = 完全逆向:记录 → .so refcount++ → 分配 state → 反序列化 → 绑定 → ACTIVE。

### 驱逐策略(可插拔)

- 接口 `evict(working_set) → victim`;M1 实现 = 窗口 T(Denning)+ GDSF 简化版(权重 = 频率 × 恢复成本,成本提示存记录 reserved 字段)。
- 硬规则:**refs > 0 的运行中函数绝不驱逐**——先 quiesce 再处置。

## 7. 服务层设计 (L4)

### 触发索引

- mmap 内排序的 `(event_id → fn_id 区间)`;事件注册时分配 u32 event_id;同事件订阅函数聚成连续区间(创造模式/事件路径互不干扰)。
- 派发路径:事件 → 区间扫描 → 逐候选查状态 → COLD 则物化 → 执行 → 结果聚合。
- M1 派发单线程(游戏 tick 语义);并行化入口:① 事件扇出时并行物化,② 索引构建并行。M2 才做全调度器。

### 所有权 / 租约

- **ModuleContext 是统一 Owner**:模块的一切资源(task/subscription/registry entry/state)挂在其下,`module.unload()` 统一处置;不搞"unregisterX()"无限堆独立 API。
- **租约替代裸引用**:跨模块持有资源 = 拿 lease;模块墓碑时 lease 持有者收到回调通知,把动态性的负担从 Mod 作者身上拿走(规避 OSGi"消费者自己处理服务消失"的已知弱点)。
- drain 协议 = quiesce:停新 → drain → 释放 → 持久化。

### 事务 / 滚动更新

```
prepare   v2 包解析:新 generation 记录就位、依赖 resolve、ABI 校验
validate  依赖闭包 + 版本约束 + 状态迁移映射表检查
commit    原子切换点:派发路由切到 v2;v1 逐个 quiesce
rollback  demote:切换失败或运行期验证失败 → 派发切回 v1(记录还在)
abort     prepare/validate 阶段失败,未提交无副作用
```

- **混合版本共存**:generation 是函数级字段 → `foo()` 走 v2、`bar()` 走 v1 天然成立(kGraft/DynAMOS 并行版本路线)。
- **状态迁移**:每个函数可选 `state_transform(v1_state → v2_state)` 钩子,validate 阶段校验、commit 阶段执行(DSU 文献公认最难的部分,做成事务的显式一步)。
- **回滚 = demote**:Myedsua 模式,不是撤销,是派发降级;v1 state 从未销毁(墓碑序列化过),恢复成本 = 反序列化。
- M1 只保留 generation 字段与格式支持,**完整实现放 M2**。

### 调度器

- M1:顺序核心 + 并行索引构建器(按模块分片)。硬指标不依赖全调度器。
- M2:完整 DAG 调度器(task 依赖/读-写集/优先级/亲和性/取消/checkpoint);并行化的是实际工作,不是盲目为每 Mod 建线程。

## 8. 里程碑路线图

| 里程碑 | 内容 | 与 MC 关系 |
|---|---|---|
| **M1(本次设计范围)** | 冷存储、紧凑索引、物化、状态机、触发索引、所有权骨架、墓碑/恢复 + 基准测试 | 零依赖,合成世界验证 |
| M2 | 事务/滚动更新完整实现、DAG 调度器、依赖图、描述符/资源 API | 零依赖 |
| M3 | 合成世界运行(模拟玩家加入/方块破坏)→ World Runtime | 零依赖 |
| M4 | JVM Bridge + 1.20.1 服务端集成(世界内动态加载) | 1.20.1 |
| M5 | Fabric/NeoForge 兼容 Provider、API 大扩展 | 1.20.1 |

## 9. M1 验收门槛(写死进 CI)

1. **冷规模**:10M 函数构建 pack,进程 RSS 增量 ≤ 80MB(预期实际 KB~MB 级,摊派 ≈ 0B/函数)。
2. **全循环延迟**:事件触发→物化→执行→闲置→墓碑→恢复→再执行 ≤ 500μs(单线程,合成函数)。
3. **热路径零检查**:进入工作集后,热函数分派开销 ≤ 直调函数指针的 1.10×(基准断言,防回归)。
4. **基准即门禁**:脚本化,CI 上跑,超阈值即失败。

## 10. 基准测试 harness

- **合成宇宙生成器**:参数化(函数数 100k/1M/10M、模块数、事件分布、触发模式),生成 pack + 合成事件流。
- **场景脚本**:S1 冷规模(RSS)→ S2 冷启动(触发物化 1k 函数延迟)→ S3 全循环 → S4 热路径对比。
- **测量**:Linux `/proc/self/status` VmRSS + mmap 访问统计(`madvise` 提示页)。

## 11. 错误处理

- 错误分层:pack 格式错/构建错 → **fail-fast**(启动校验);运行时物化失败 → **事件降级**(跳过该函数 + 记录诊断);事务错误 → 回滚。
- M1 不引异常系统:返回码 + 诊断日志。

## 12. 测试策略

- 单元:状态机非法转移拒绝、记录 round-trip、索引查询正确性、事件→函数解析。
- 属性/模糊:构建器随机宇宙 vs 朴素实现对照。
- 基准门禁:阈值即测试。
- 工具:cmake + ctest,零外部依赖。

## 13. 仓库布局

```
mosaic/
├── include/mosaic/    # 稳定 API 头
├── src/core/          # L0-L3:pack/索引/工作集/状态机
├── src/services/      # L4:触发索引/所有权/驱逐
├── src/build/         # pack 构建器(并行)
├── bench/             # 合成宇宙 + 基准场景
├── tests/             # 单元/属性测试
├── ci/                # 门禁脚本
└── docs/
```

## 14. 文献依据

- **工作集模型**:Denning 的 working set 定义与 locality 原则(1967/1968, 2021 "Working Set Analytics" 教程);基于局部性的工作集内存管理近似最优、免疫 thrashing → 支撑不变量 3 与驱逐策略的窗口模型。
- **Serverless 冷启动/温池**:FaasCache (ASPLOS'22) 温池=缓存、GDSF 策略;FaaShadow (ToC 2026) 驱逐优先级与执行状态解耦;CIDRE 并发感知驱逐 → 支撑正交轴设计与可插拔驱逐接口。
- **动态软件更新 (DSU)**:IET 系统映射研究(27 模型/状态迁移技术目录);kGraft/DynAMOS 并行版本、逐线程选版本;Tranquility 低干扰静默;Myedsua 多版本执行 + promote/demote 回滚;UpStare 栈重建 → 支撑事务模型与混合版本共存。
- **OSGi Core r8**:bundle 生命周期(INSTALLED→RESOLVED→STARTING→ACTIVE)、resolve/wire 依赖绑定、Declarative Services 惰性激活;其"消费者自担服务消失"弱点由所有权/租约机制规避 → 支撑状态机、resolve 步骤与 L4 所有权设计。
- **紧凑索引**:Two-level massive string dictionaries (IS 2025,十亿键 195MB,索引内存挤占页缓存的教训);Tiny Pointer Hash Tables (2026,单缓存未命中查询);Gog & Petri rank/select (2014) → 支撑 L1 极小内存索引与分列式演进路径。

## 15. 未来演进方向(非 M1 承诺)

- 分列式冷存储 + succinct rank/select(十亿级规模时演进)。
- ML 驱动的驱逐策略(接口已可插拔)。
- 多线程事件扇出物化、DAG 调度器下的并行派发。
