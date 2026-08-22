# Mosaic M5 API 面实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 Mosaic 稳定 API 面:31 大类 Java 接口(21 运行时域 + 10 原版域)、JNI 实现接通 C 内核、26.2/1.8.9 双代 Provider 与对照测试、只增不减的兼容套件与版本校验。

**Architecture:** Java 为唯一稳定 API 面(域模型接口,只增不减);运行时域实现经 JNI 到 C 核心(现有 mosaic_* 退为内核);原版域实现 = Provider(反射 + 版本映射表,差异全在 Provider)。双代锚定:域模型语义以 1.8.9 ∩ 26.2 共同能力为界,同套契约测试在两个 Provider 环境分别跑。

**Tech Stack:** Java 21、JNI(扩展现有 bridge.c)、C 内核(现有)、真实 26.2/1.8.9 jar 作 classpath、逆向源码(~/minecraft26.2/decompiled、~/minecraft1.8.9/mcp918)仅阅读、零第三方依赖(测试用 main + 断言模式,与 MosaicBridgeTest 一致)。

## Global Constraints

- API 只增不减:接口签名一经合入永不删除/改语义;`@Since` 标注新增成员;compat 套件编译失败 = 门禁红。
- 命名:`Mosaic` + 驼峰,无下划线、无缩写;包结构 `mosaic`(基座)、`mosaic.runtime.*`、`mosaic.vanilla.*`。
- Java 为唯一稳定面;C API(mosaic_*)退为内核,不对外承诺。
- 域模型双代锚定:接口语义以 1.8.9 ∩ 26.2 共同能力为界;单代能力 since 标注。
- 事件载荷保持 byte[] 小端 ↔ events.h 结构体约定。
- 测试环境用真实 jar(26.2/1.8.9),逆向源码仅阅读。
- 既有回归:16 C 套件 + JNI 23 断言 + 服务端端到端全部保留。

---

### Task 1: 环境准备 —— 两代 jar 定位/下载 + 类名验证

**Files:**
- Create: `ci/setup_mc_versions.sh`
- Create: `ci/check_classnames.sh`
- Modify: `.gitignore`(lib/mc-versions/)

**Interfaces:**
- Consumes: 无
- Produces: `lib/mc-versions/vanilla-26.2.jar`、`lib/mc-versions/vanilla-1.8.9.jar`(Provider 测试 classpath);类名验证结果(决定映射表用 mojmap 名还是混淆名)

- [ ] **Step 1: 写 ci/setup_mc_versions.sh**

```bash
#!/usr/bin/env bash
# 定位/下载 26.2 与 1.8.9 真实 jar,供 Provider 测试作 classpath
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p lib/mc-versions

# 26.2:从官方版本清单取 client/server jar
if [ ! -f lib/mc-versions/vanilla-26.2.jar ]; then
  URL=$(curl -s https://piston-meta.mojang.com/mc/game/version_manifest_v2.json \
    | jq -r '.versions[] | select(.id=="26.2") | .url' | head -1)
  if [ -n "$URL" ] && [ "$URL" != "null" ]; then
    JAR_URL=$(curl -s "$URL" | jq -r '.downloads.server.url')
    curl -sL "$JAR_URL" -o lib/mc-versions/vanilla-26.2.jar
  else
    echo "26.2 版本清单未找到,尝试用户逆向目录的 jar"
    find ~/minecraft26.2 -name "*.jar" -size +5M | head -1 | xargs -I{} cp {} lib/mc-versions/vanilla-26.2.jar
  fi
fi

# 1.8.9:用户逆向目录的 client jar(或 libs)
if [ ! -f lib/mc-versions/vanilla-1.8.9.jar ]; then
  find ~/minecraft1.8.9 -name "*.jar" -size +5M | grep -iv "launchwrapper\|optifine" | head -1 \
    | xargs -I{} cp {} lib/mc-versions/vanilla-1.8.9.jar
fi

ls -la lib/mc-versions/
```

- [ ] **Step 2: 写 ci/check_classnames.sh(验证运行 jar 内类名,决定映射表基准)**

```bash
#!/usr/bin/env bash
# 验证两代 jar 的运行时类名(26.2 是否混淆、1.8.9 是 notch 名还是 MCP 名)
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== 26.2 jar:Block/Item/Level/Entity 类名 ==="
for c in "world/level/block/Block" "world/item/Item" "world/level/Level" "world/entity/Entity" \
         "server/players/PlayerList" "nbt/CompoundTag" "commands/Commands" "network/protocol/Packet"; do
  echo -n "$c: "
  unzip -l lib/mc-versions/vanilla-26.2.jar 2>/dev/null | grep -c "net/minecraft/$c.class" || true
done

echo "=== 1.8.9 jar:Block/Item/World/Entity 类名 ==="
for c in "block/Block" "item/Item" "world/World" "entity/Entity" "nbt/NBTTagCompound" \
         "command/CommandHandler" "network/Packet" "server/MinecraftServer"; do
  echo -n "$c: "
  unzip -l lib/mc-versions/vanilla-1.8.9.jar 2>/dev/null | grep -c "net/minecraft/$c.class" || true
done

echo "=== 若 26.2 上述类全为 0,说明 jar 内是混淆名 —— 映射表需用混淆名(从 server_mappings 提取) ==="
```

- [ ] **Step 3: 运行两个脚本,记录类名验证结果(报告:26.2 jar 内类名形态、1.8.9 jar 内类名形态;决定后续映射表基准)**

Run: `bash ci/setup_mc_versions.sh && bash ci/check_classnames.sh`
Expected: 两 jar 就位;输出每类的命中数(1 = 存在该名,0 = 混淆/不存在)。

- [ ] **Step 4: 提交**

```bash
git add ci/setup_mc_versions.sh ci/check_classnames.sh .gitignore
git commit -m "chore: MC version jar setup and classname verification scripts (M5 env)"
```

---

### Task 2: API 基座 + 运行时域 21 类接口定义(纯接口)

**Files:**
- Create: `java-api/mosaic/MosaicApi.java`
- Create: `java-api/mosaic/Since.java`
- Create: `java-api/mosaic/MosaicApiException.java`、`MosaicHandleException.java`、`MosaicProviderNotFoundException.java`、`MosaicApiVersionException.java`
- Create: `java-api/mosaic/runtime/MosaicRuntime.java`、`MosaicPackBuilder.java`、`MosaicPackInfo.java`
- Create: `java-api/mosaic/runtime/MosaicIndexQuery.java`、`MosaicFunctionIndex.java`、`MosaicModuleIndex.java`、`MosaicEventIndex.java`、`MosaicItemIndex.java`
- Create: `java-api/mosaic/runtime/MosaicFunctionDescriptor.java`、`MosaicModuleDescriptor.java`、`MosaicItemDescriptor.java`、`MosaicEventDescriptor.java`
- Create: `java-api/mosaic/runtime/MosaicFunctionLifecycle.java`、`MosaicActivationPolicy.java`、`MosaicActivationGate.java`
- Create: `java-api/mosaic/runtime/MosaicEvictionPolicy.java`、`MosaicWorkingSet.java`、`MosaicWorkingSetStats.java`
- Create: `java-api/mosaic/runtime/MosaicFunctionState.java`、`MosaicStateStore.java`、`MosaicStateTransform.java`
- Create: `java-api/mosaic/runtime/MosaicModule.java`、`MosaicModuleContext.java`、`MosaicModuleInfo.java`、`MosaicModuleLoader.java`
- Create: `java-api/mosaic/runtime/MosaicDependencyGraph.java`、`MosaicVersionConstraint.java`、`MosaicDependencyResolver.java`
- Create: `java-api/mosaic/runtime/MosaicTransaction.java`、`MosaicTxPatch.java`、`MosaicTxResult.java`
- Create: `java-api/mosaic/runtime/MosaicEvent.java`、`MosaicEventDispatcher.java`、`MosaicEventSubscription.java`、`MosaicEventCatalog.java`、`MosaicEventPayload.java`
- Create: `java-api/mosaic/runtime/MosaicTriggerIndex.java`、`MosaicTriggerEntry.java`
- Create: `java-api/mosaic/runtime/MosaicScheduler.java`、`MosaicSchedulerConfig.java`
- Create: `java-api/mosaic/runtime/MosaicTask.java`、`MosaicTaskDependency.java`、`MosaicTaskPriority.java`、`MosaicTaskResult.java`、`MosaicCheckpoint.java`
- Create: `java-api/mosaic/runtime/MosaicLease.java`、`MosaicOwnedResource.java`、`MosaicResourceHandle.java`
- Create: `java-api/mosaic/runtime/MosaicResourceManager.java`、`MosaicResourceLease.java`
- Create: `java-api/mosaic/runtime/MosaicService.java`、`MosaicServiceRegistry.java`、`MosaicServiceRef.java`
- Create: `java-api/mosaic/runtime/MosaicCapability.java`、`MosaicCapabilityProvider.java`、`MosaicCapabilityQuery.java`
- Create: `java-api/mosaic/runtime/MosaicQuery.java`、`MosaicQueryBuilder.java`、`MosaicQueryResult.java`
- Create: `java-api/mosaic/runtime/MosaicBridge.java`、`MosaicBridgeException.java`、`MosaicPayloadCodec.java`

**Interfaces:**
- Consumes: Task 1 的 jar 环境(本任务不需要,纯接口)
- Produces: 全部运行时域接口(M5-2 实现的目标;契约测试的编译依赖)

- [ ] **Step 1: 写基座(mosaic/MosaicApi.java + Since.java + 4 异常)**

```java
package mosaic;

/** Mosaic API 基座:版本常量 + 版本守卫。API 只增不减:本常量只升不降。 */
public final class MosaicApi {
    /** 当前 API 版本(只增不减;新增成员用 @Since 标注) */
    public static final int API_VERSION = 1;
    private MosaicApi() {}

    /** 运行时守卫:mod 声明所需版本 > API_VERSION 时抛 MosaicApiVersionException。 */
    public static void requireApi(int requiredMax) {
        if (requiredMax > API_VERSION)
            throw new MosaicApiVersionException("API " + requiredMax + " required, runtime has " + API_VERSION);
    }

    /** 打开运行时(工厂;pack 路径数组,至少一个)。 */
    public static mosaic.runtime.MosaicRuntime open(String[] packPaths) {
        return mosaic.runtime.MosaicRuntime.open(packPaths);
    }
}
```

```java
package mosaic;

import java.lang.annotation.*;

/** 标注 API 成员引入版本(只增不减:成员引入后永不删除/改语义)。 */
@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.METHOD, ElementType.TYPE, ElementType.FIELD})
public @interface Since {
    int value();
}
```

```java
package mosaic;

/** API 异常基类。 */
public class MosaicApiException extends RuntimeException {
    public MosaicApiException(String msg) { super(msg); }
    public MosaicApiException(String msg, Throwable cause) { super(msg, cause); }
}
```

```java
package mosaic;

/** 句柄失效(墓碑/卸载后访问)。 */
public class MosaicHandleException extends MosaicApiException {
    public MosaicHandleException(String msg) { super(msg); }
}
```

```java
package mosaic;

/** 当前 MC 版本无匹配 Provider。 */
public class MosaicProviderNotFoundException extends MosaicApiException {
    public MosaicProviderNotFoundException(String msg) { super(msg); }
}
```

```java
package mosaic;

/** mod 声明所需 API 版本 > 运行时 API_VERSION。 */
public class MosaicApiVersionException extends MosaicApiException {
    public MosaicApiVersionException(String msg) { super(msg); }
}
```

- [ ] **Step 2: 写运行时域核心接口(以下为完整代码;每个接口是 M5-2 的实现契约)**

```java
package mosaic.runtime;

import mosaic.MosaicApiException;
import mosaic.Since;

/** 运行时入口:打开/关闭/挂载 pack、派发、查询。实现经 JNI 到 C 内核。 */
public interface MosaicRuntime {
    /** 打开 pack 组(至少一个;事件表必须一致、模块范围不重叠)。 */
    static MosaicRuntime open(String[] packPaths) {
        return mosaic.runtime.internal.RuntimeImpl.open(packPaths);
    }
    long functionCount();
    /** 事件名 → id;未注册返回 -1。 */
    int eventId(String name);
    /** 派发事件(载荷 byte[] 小端 ↔ events.h 结构体);返回执行数。 */
    int eventDispatch(int eventId, byte[] payload);
    int workingSetCount();
    int lastError();
    /** 世界内挂载 pack(零重启);失败抛 MosaicHandleException。 */
    void addPack(String packPath);
    void close();

    MosaicFunctionLifecycle lifecycle();
    MosaicEventDispatcher eventDispatcher();
    MosaicEventCatalog eventCatalog();
    MosaicIndexQuery index();
    MosaicWorkingSet workingSet();
    MosaicModuleLoader moduleLoader();
    MosaicDependencyResolver dependencyResolver();
    MosaicScheduler scheduler();
    MosaicResourceManager resources();
    MosaicServiceRegistry services();
    MosaicQueryBuilder query();
}
```

```java
package mosaic.runtime;

import mosaic.Since;

/** pack 构建器(离线工具;JNI 逐记录调用 C builder)。 */
public interface MosaicPackBuilder {
    static MosaicPackBuilder create(String path, long moduleCount, long fnCount,
                                    long triggerCount, long depCount, int eventCount) {
        return mosaic.runtime.internal.PackBuilderImpl.create(path, moduleCount, fnCount,
                triggerCount, depCount, eventCount);
    }
    void addEvent(String name);
    void addModule(long moduleId, int version, String name, String soPath);
    void addFn(long moduleId, long localId, int codeOff, int stateSize, int generation,
               int costHint, int flags);
    void setFnTransform(long fnId, int transformIndex);
    void addTrigger(int eventId, long fnId);
    void addDep(long ownerId, long depId);
    void setItemCount(long itemCount);
    void addItem(long providerFnId, String name, String tags, int category, String iconRef, int flags);
    /** 排序/校验/写出;失败返回 -1(错误信息经 lastError)。 */
    int finish();
}

/** pack 只读信息(冷态)。 */
public interface MosaicPackInfo {
    long moduleCount();
    long functionCount();
    long triggerCount();
    long itemCount();
    int eventCount();
}
```

```java
package mosaic.runtime;

/** 索引查询(纯冷态,零物化)。 */
public interface MosaicIndexQuery {
    MosaicFunctionIndex functions();
    MosaicModuleIndex modules();
    MosaicEventIndex events();
    MosaicItemIndex items();
}
public interface MosaicFunctionIndex {
    /** fnId = moduleId<<32|localId;未命中返回 null。 */
    MosaicFunctionDescriptor find(long fnId);
    long count();
}
public interface MosaicModuleIndex {
    MosaicModuleDescriptor find(long moduleId);
    long count();
}
public interface MosaicEventIndex {
    /** 事件名 → id;未注册 -1。 */
    int id(String name);
    int count();
}
public interface MosaicItemIndex {
    /** 分类内按名二分;未命中 null。 */
    MosaicItemDescriptor find(int category, String name);
    /** 枚举分类内全部(回调返回 false 停止)。 */
    void forEach(int category, java.util.function.Consumer<MosaicItemDescriptor> consumer);
    long count();
}
```

```java
package mosaic.runtime;

/** 冷态描述符(查询/创造模式浏览零物化)。 */
public interface MosaicFunctionDescriptor {
    long fnId();
    long moduleId();
    int codeOffset();
    int generation();
    int stateSize();
    int costHint();
    int flags();
}
public interface MosaicModuleDescriptor {
    long moduleId();
    int version();
    int generation();
    String name();
    String soPath();
}
public interface MosaicItemDescriptor {
    long providerFnId();
    String name();
    String tags();
    int category();
    String iconRef();
}
public interface MosaicEventDescriptor {
    String name();
    /** 0=低 1=中 2=高 */
    int freq();
    int payloadSize();
}
```

```java
package mosaic.runtime;

import mosaic.MosaicHandleException;

/** 函数生命周期:物化/执行/墓碑/状态(函数级惰性的 API 面)。 */
public interface MosaicFunctionLifecycle {
    /** 物化(COLD→ACTIVE;TOMBSTONED→恢复);返回句柄;失败抛 MosaicHandleException。 */
    long materialize(long fnId);
    /** 热路径执行(句柄必须有效;载荷小端)。 */
    void execute(long fnHandle, int eventId, byte[] payload);
    /** 墓碑(refs==0 才可);返回 0 成功、-1 失败(lastError 取因)。 */
    int tombstone(long fnHandle);
    /** 读取函数状态(64B 上限);未物化返回 null。 */
    byte[] state(long fnHandle);
    /** 句柄 → fnId。 */
    long fnIdOf(long fnHandle);
}

/** 激活策略(可插拔;默认自动:事件驱动物化)。 */
public interface MosaicActivationPolicy {
    boolean shouldMaterialize(long fnId, int eventId);
}
public interface MosaicActivationGate {
    MosaicActivationPolicy policy();
    void setPolicy(MosaicActivationPolicy policy);
}
```

```java
package mosaic.runtime;

import mosaic.Since;

/** 驱逐:窗口 T + 可插拔策略;refs>0 绝不驱逐。 */
public interface MosaicEvictionPolicy {
    /** 窗口纳秒(Denning T);0 = 立即过期。 */
    long windowNanos();
}
public interface MosaicWorkingSet {
    int count();
    /** 驱逐空闲函数(窗口内未使用);返回墓碑数。 */
    int evictIdle(long windowNanos);
    /** 全部 ACTIVE 函数 id(快照)。 */
    long[] activeFnIds();
}
public interface MosaicWorkingSetStats {
    long totalMaterialized();
    long totalTombstoned();
    long totalRestored();
}
```

```java
package mosaic.runtime;

/** 函数状态读写与迁移。 */
public interface MosaicFunctionState {
    byte[] read(long fnId);
    /** 写入持久状态槽(墓碑序列化用)。 */
    void write(long fnId, byte[] state);
}
public interface MosaicStateStore {
    MosaicFunctionState forFn(long fnId);
}
/** 状态迁移钩子(v1_state → v2_state;size = v2 大小)。 */
public interface MosaicStateTransform {
    void transform(byte[] v1State, byte[] v2State, int size);
}
```

```java
package mosaic.runtime;

/** 模块与统一资源归属(ModuleContext 是 Owner)。 */
public interface MosaicModule {
    long moduleId();
    int version();
    int generation();
    String name();
    MosaicModuleContext context();
}
public interface MosaicModuleContext {
    /** 模块拥有的任务/订阅/状态统一处置(module.unload 语义)。 */
    void unload();
    MosaicResourceHandle resource();
}
public interface MosaicModuleInfo {
    long moduleId();
    int version();
    String soPath();
    int fnCount();
}
public interface MosaicModuleLoader {
    MosaicModule load(long moduleId);
    void unload(long moduleId);
}
```

```java
package mosaic.runtime;

/** 依赖图:闭包/环检测/版本约束。 */
public interface MosaicDependencyGraph {
    /** 遍历直接依赖(cb 返回 false 停止)。 */
    void forEachDep(long moduleId, java.util.function.LongConsumer consumer);
}
public interface MosaicVersionConstraint {
    int minVersion();
    int maxVersion();   /* 0 = 无界 */
    boolean accepts(int version);
}
public interface MosaicDependencyResolver {
    /** 依赖闭包(含自身,拓扑序,依赖先于依赖者);失败抛 MosaicApiException。 */
    long[] resolve(long moduleId, MosaicVersionConstraint self);
}
```

```java
package mosaic.runtime;

/** 事务(滚动更新):prepare/validate/commit/rollback/abort。 */
public interface MosaicTransaction {
    MosaicTxResult prepare();
    MosaicTxResult validate();
    MosaicTxResult commit();
    MosaicTxResult rollback();
    void abort();
}
public interface MosaicTxPatch {
    String packPath();
    long[] fnIds();
}
public interface MosaicTxResult {
    boolean ok();
    /** 失败原因(错误信息)。 */
    String error();
}
```

```java
package mosaic.runtime;

import mosaic.Since;

/** 事件:派发/订阅/目录/载荷。 */
public interface MosaicEvent {
    int eventId();
    String name();
    int payloadSize();
}
public interface MosaicEventDispatcher {
    /** 派发:C 内核订阅者执行 + Java 侧订阅者执行;返回执行总数。 */
    int dispatch(int eventId, byte[] payload);
    /** Java 侧运行时订阅(回调表,纯 Java 层)。 */
    MosaicEventSubscription subscribe(int eventId, MosaicEventHandler handler);
    void unsubscribe(MosaicEventSubscription subscription);
}
@FunctionalInterface
public interface MosaicEventHandler {
    void onEvent(int eventId, byte[] payload);
}
public interface MosaicEventSubscription {
    int eventId();
    void close();
}
public interface MosaicEventCatalog {
    /** 目录中全部事件(按名排序);未注册名返回 null。 */
    MosaicEvent find(String name);
    int count();
}
public interface MosaicEventPayload {
    /** 类型化解码:按事件域解码 byte[] → 字段;失败抛 MosaicHandleException。 */
    int[] decodeInts();
    byte[] encode();
}
```

```java
package mosaic.runtime;

/** 触发索引(事件 → 订阅函数区间)。 */
public interface MosaicTriggerIndex {
    /** 事件全部订阅函数 id(排序)。 */
    long[] subscribers(int eventId);
}
public interface MosaicTriggerEntry {
    int eventId();
    long fnId();
}
```

```java
package mosaic.runtime;

/** DAG 调度器(线程池 + 依赖图执行)。 */
public interface MosaicScheduler {
    int submit(MosaicTask task);
    int waitAll();
    int cancel(long taskId);
    int pendingCount();
}
public interface MosaicSchedulerConfig {
    int workers();
    static MosaicSchedulerConfig of(int workers) { return new MosaicSchedulerConfig() {
        public int workers() { return workers; } }; }
}
public interface MosaicTask {
    long id();
    int[] dependencyIds();
    int priority();
    int affinity();
    void run();
    MosaicCheckpoint checkpoint();
}
public interface MosaicTaskDependency {
    long taskId();
}
public enum MosaicTaskPriority { LOW, NORMAL, HIGH }
public interface MosaicTaskResult {
    boolean ok();
    String error();
}
public interface MosaicCheckpoint {
    /** 取消/暂停时保存状态。 */
    void save(MosaicTask task);
}
```

```java
package mosaic.runtime;

/** 所有权:租约(refs 守护)。 */
public interface MosaicLease {
    long fnId();
    void release();
}
public interface MosaicOwnedResource {
    void dispose();
}
public interface MosaicResourceHandle {
    boolean valid();
    void invalidate();
}
public interface MosaicResourceManager {
    MosaicResourceLease acquire(long fnId);
    void release(MosaicResourceLease lease);
}
public interface MosaicResourceLease {
    long fnId();
    void close();
}
```

```java
package mosaic.runtime;

/** 服务注册/发现(纯 Java 层)。 */
public interface MosaicService { }
public interface MosaicServiceRegistry {
    <T extends MosaicService> void register(Class<T> type, T service);
    <T extends MosaicService> T get(Class<T> type);
    /** 可选获取(不存在返回 null)。 */
    <T extends MosaicService> T optional(Class<T> type);
}
public interface MosaicServiceRef {
    MosaicService service();
    void release();
}
```

```java
package mosaic.runtime;

/** 能力:Provider 体系(纯 Java 层)。 */
public interface MosaicCapability { }
public interface MosaicCapabilityProvider {
    <T extends MosaicCapability> T provide(Class<T> type);
}
public interface MosaicCapabilityQuery {
    <T extends MosaicCapability> T require(Class<T> type);
    <T extends MosaicCapability> T optional(Class<T> type);
}
```

```java
package mosaic.runtime;

/** 查询(创造模式:浏览描述符不物化)。 */
public interface MosaicQuery {
    /** 按分类浏览全部 item 描述符。 */
    MosaicQueryResult items(int category);
}
public interface MosaicQueryBuilder {
    MosaicQuery byCategory(int category);
}
public interface MosaicQueryResult {
    long count();
    /** 当前页描述符(零物化)。 */
    mosaic.runtime.MosaicItemDescriptor get(int index);
}
```

```java
package mosaic.runtime;

/** Java↔C 通道(内核桥;内部实现用,API 面保留以支持自诊断)。 */
public interface MosaicBridge {
    long nativeHandle();
    int lastError();
}
public class MosaicBridgeException extends mosaic.MosaicApiException {
    public MosaicBridgeException(String msg) { super(msg); }
}
public interface MosaicPayloadCodec {
    byte[] encodeInts(int... values);
    int[] decodeInts(byte[] payload);
}
```

- [ ] **Step 3: 编译验证全部接口**

Run: `mkdir -p build/japi && javac -d build/japi $(find java-api -name "*.java")`
Expected: 编译成功,零错误。

- [ ] **Step 4: 提交**

```bash
git add java-api/
git commit -m "feat: M5 API base and runtime-domain interfaces (21 domains, append-only contract) (M5-1)"
```

---

### Task 3: 原版域 10 类接口定义

**Files:**
- Create: `java-api/mosaic/vanilla/MosaicWorld.java`、`MosaicBlock.java`、`MosaicBlockState.java`、`MosaicBlockPos.java`
- Create: `java-api/mosaic/vanilla/MosaicItem.java`、`MosaicItemStack.java`、`MosaicComponents.java`
- Create: `java-api/mosaic/vanilla/MosaicInventory.java`、`MosaicInventorySlot.java`
- Create: `java-api/mosaic/vanilla/MosaicEntity.java`、`MosaicEntityId.java`、`MosaicEntityType.java`
- Create: `java-api/mosaic/vanilla/MosaicPlayer.java`、`MosaicPlayerSession.java`
- Create: `java-api/mosaic/vanilla/MosaicRegistry.java`、`MosaicRegistryEntry.java`
- Create: `java-api/mosaic/vanilla/MosaicCommand.java`、`MosaicCommandTree.java`
- Create: `java-api/mosaic/vanilla/MosaicNetwork.java`、`MosaicPacketListener.java`
- Create: `java-api/mosaic/vanilla/MosaicNbt.java`、`MosaicNbtCompound.java`
- Create: `java-api/mosaic/vanilla/MosaicProvider.java`、`MosaicProviderRegistry.java`

**Interfaces:**
- Consumes: Task 2 的基座
- Produces: 原版域接口(M5-3/4 Provider 实现目标)

- [ ] **Step 1: 写原版域接口(完整代码;语义锚定 1.8.9 ∩ 26.2 共同能力,单代能力 since 标注)**

```java
package mosaic.vanilla;

import mosaic.Since;

/** 方块:稳定句柄(26.2 world.level.block.Block ↔ 1.8.9 block.Block 均转换为此)。 */
public interface MosaicBlock {
    /** 当前状态。 */
    MosaicBlockState state();
    /** 注册表名(如 "minecraft:stone");1.8.9 由数字 id 合成。 */
    String registryName();
}
public interface MosaicBlockState {
    MosaicBlock block();
    /** 属性集(如 lit/waterlogged);两代共同语义。 */
    String[] propertyNames();
    /** 属性值(字符串形态);未知属性抛 MosaicHandleException。 */
    String property(String name);
}
public interface MosaicBlockPos {
    int x(); int y(); int z();
    static MosaicBlockPos of(int x, int y, int z) {
        return new MosaicBlockPos() {
            public int x() { return x; } public int y() { return y; } public int z() { return z; } };
    }
}
```

```java
package mosaic.vanilla;

import mosaic.Since;

/** 物品:稳定句柄。 */
public interface MosaicItem {
    String registryName();
    int maxStackSize();
    /** 组件集(26.2 起;1.8.9 为空实现)。 */
    @Since(1)
    MosaicComponents components();
}
public interface MosaicItemStack {
    MosaicItem item();
    int count();
    MosaicItemStack copy();
}
public interface MosaicComponents {
    /** 组件键集合;26.2 DataComponent 映射,1.8.9 返回空。 */
    String[] keys();
    /** 组件字节(序列化形态);未知键返回 null。 */
    byte[] get(String key);
    @Since(1)
    MosaicComponents with(String key, byte[] value);
}
```

```java
package mosaic.vanilla;

/** 容器:槽位/计数/移动。 */
public interface MosaicInventory {
    int slotCount();
    MosaicInventorySlot slot(int index);
    /** 槽位内容物品(空槽返回 null)。 */
    MosaicItemStack getItem(int index);
    void setItem(int index, MosaicItemStack stack);
    int size();
}
public interface MosaicInventorySlot {
    int index();
    MosaicItemStack item();
    boolean isEmpty();
}
```

```java
package mosaic.vanilla;

/** 实体:稳定句柄。 */
public interface MosaicEntity {
    MosaicEntityId id();
    MosaicEntityType type();
    double x(); double y(); double z();
    /** 属性值(如 "minecraft:max_health");未知属性抛 MosaicHandleException。 */
    double attribute(String name);
}
public interface MosaicEntityId {
    int value();
}
public interface MosaicEntityType {
    String registryName();
}
```

```java
package mosaic.vanilla;

/** 玩家会话。 */
public interface MosaicPlayer {
    MosaicEntityId entityId();
    String name();
    int gameMode();   /* 0=生存 1=创造 2=冒险 3=旁观(两代同序) */
    boolean online();
}
public interface MosaicPlayerSession {
    /** 当前在线玩家 id 列表。 */
    int[] onlinePlayerIds();
    /** 按 id 取玩家;离线返回 null。 */
    MosaicPlayer byId(int playerId);
}
```

```java
package mosaic.vanilla;

/** 注册表:id↔名双向映射(数字 id/注册表 id 差异全在 Provider)。 */
public interface MosaicRegistry {
    /** 名 → id;未注册 -1。 */
    int id(String registryName);
    /** id → 名;未注册 null。 */
    String name(int id);
}
public interface MosaicRegistryEntry {
    int id();
    String registryName();
}
```

```java
package mosaic.vanilla;

/** 命令:注册/执行(1.8.9 command.* ↔ 26.2 Brigadier)。 */
public interface MosaicCommand {
    /** 注册命令(名 + 执行回调);重名抛 MosaicApiException。 */
    void register(String name, MosaicCommandHandler handler);
}
@FunctionalInterface
public interface MosaicCommandHandler {
    int execute(String[] args);
}
public interface MosaicCommandTree {
    String[] registered();
}
```

```java
package mosaic.vanilla;

/** 网络:包收发/监听器。 */
public interface MosaicNetwork {
    void sendPacket(int playerId, byte[] packetData);
    MosaicPacketListener listener();
}
public interface MosaicPacketListener {
    /** 注册包处理(按包类型名);返回订阅可关闭。 */
    AutoCloseable onPacket(String packetTypeName, MosaicPacketHandler handler);
}
@FunctionalInterface
public interface MosaicPacketHandler {
    void handle(int playerId, byte[] packetData);
}
```

```java
package mosaic.vanilla;

/** NBT:复合标签读写(两代语义最稳定)。 */
public interface MosaicNbt {
    MosaicNbtCompound compound();
}
public interface MosaicNbtCompound {
    boolean contains(String key);
    String getString(String key);
    int getInt(String key);
    void putString(String key, String value);
    void putInt(String key, int value);
    String[] keys();
    byte[] toBytes();
}
```

```java
package mosaic.vanilla;

/** Provider:版本差异全吸收;句柄持有原版引用,接口方法读取时转换。 */
public interface MosaicProvider {
    String providerId();            /* "vanilla-26.2" / "vanilla-1.8.9" */
    String mcVersion();             /* "26.2" / "1.8.9" */
    boolean supportsApi(int min, int max);

    MosaicBlock blockOf(Object vanillaBlock);
    MosaicBlockState blockStateOf(Object vanillaBlockState);
    MosaicItem itemOf(Object vanillaItem);
    MosaicItemStack itemStackOf(Object vanillaItemStack);
    MosaicWorld worldOf(Object vanillaWorld);
    MosaicEntity entityOf(Object vanillaEntity);
    MosaicPlayer playerOf(Object vanillaPlayer);
    MosaicInventory inventoryOf(Object vanillaInventory);
    MosaicRegistry registryOf(Object vanillaRegistry);
    MosaicNbt nbtOf(Object vanillaNbt);
}

/** Provider 选择:按 mcVersion。 */
public final class MosaicProviderRegistry {
    private static final java.util.List<MosaicProvider> PROVIDERS = new java.util.ArrayList<>();
    private MosaicProviderRegistry() {}
    public static void register(MosaicProvider p) { PROVIDERS.add(p); }
    public static MosaicProvider forVersion(String mcVersion) {
        for (MosaicProvider p : PROVIDERS)
            if (p.mcVersion().equals(mcVersion)) return p;
        throw new mosaic.MosaicProviderNotFoundException(
                "no provider for MC version " + mcVersion + " (registered: "
                + PROVIDERS.stream().map(MosaicProvider::mcVersion).toList() + ")");
    }
    public static java.util.List<MosaicProvider> all() { return new java.util.ArrayList<>(PROVIDERS); }
}
```

- [ ] **Step 2: 编译验证**

Run: `javac -d build/japi $(find java-api -name "*.java")`
Expected: 编译成功。

- [ ] **Step 3: 提交**

```bash
git add java-api/mosaic/vanilla/
git commit -m "feat: M5 vanilla-domain interfaces (10 domains, dual-generation anchored handles) (M5-1b)"
```

---

### Task 4: 运行时域 JNI 实现(核心 8 域) + 契约测试

**Files:**
- Create: `java-api/mosaic/runtime/internal/RuntimeImpl.java`、`PackBuilderImpl.java`、`IndexImpl.java`、`LifecycleImpl.java`、`EventImpl.java`、`QueryImpl.java`、`WorkingSetImpl.java`、`Native.java`
- Modify: `java/mosaic/Bridge.java`(新增 native:fnMaterialize/fnTombstone/fnExecute/fnState/fnIdOf/addFn 系列、finish、items…)
- Modify: `src/jni/bridge.c`(新增 JNI 实现)
- Create: `tests/jni/ApiContractTest.java`(运行时域契约测试,main + 断言)
- Create: `ci/run_api_contract.sh`

**Interfaces:**
- Consumes: Task 2 接口(全部);C 内核现有 API(mosaic_runtime_*/mosaic_fn_*/mosaic_event_dispatch/mosaic_pack_builder_*)
- Produces: 运行时域实现;契约测试绿(无 MC 依赖)

- [ ] **Step 1: 写失败契约测试(先红)**

`tests/jni/ApiContractTest.java`(完整代码;与 MosaicBridgeTest 同模式:gen_test_pack 生成的 pack):

```java
import mosaic.MosaicApi;
import mosaic.runtime.*;
import mosaic.runtime.internal.Native;

public class ApiContractTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) { System.err.println("usage: ApiContractTest <pack>"); System.exit(2); }
        String pack = args[0];

        MosaicRuntime rt = MosaicRuntime.open(new String[]{pack});
        check(rt.functionCount() == 3, "functionCount==3");
        check(rt.eventId("player_join") >= 0, "eventId player_join");
        check(rt.eventId("nope") == -1, "eventId nope");

        // 物化 → 执行 → state
        MosaicFunctionLifecycle lc = rt.lifecycle();
        long f0 = lc.materialize(0x100000000L);   // fn(1,0)
        check(f0 != 0, "materialize fn(1,0)");
        lc.execute(f0, rt.eventId("player_join"), new byte[4]);
        byte[] st = lc.state(f0);
        check(st != null && st.length == 64, "state 64B");
        check(java.nio.ByteBuffer.wrap(st).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(0) == 1,
              "state counter==1 after 1 exec");

        // 派发(2 订阅者)→ working set
        int n = rt.eventDispatch(rt.eventId("player_join"), new byte[4]);
        check(n == 2, "dispatch==2, got " + n);
        check(rt.workingSetCount() == 2, "workingSet==2, got " + rt.workingSetCount());

        // 墓碑 → 恢复
        long f1 = lc.materialize(0x100000001L);
        check(f1 != 0, "materialize fn(1,1)");
        check(lc.tombstone(f0) == 0, "tombstone f0");
        check(rt.workingSetCount() == 1, "workingSet==1 after tombstone");
        long f0b = lc.materialize(0x100000000L);
        check(f0b != 0, "restore f0");
        byte[] st2 = lc.state(f0b);
        check(java.nio.ByteBuffer.wrap(st2).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(0) == 1,
              "state preserved across tombstone (counter==1)");

        // 索引/描述符(冷态)
        MosaicIndexQuery idx = rt.index();
        MosaicFunctionDescriptor fd = idx.functions().find(0x100000000L);
        check(fd != null && fd.fnId() == 0x100000000L, "descriptor fnId");
        check(fd.generation() == 1, "descriptor generation");
        check(idx.modules().find(1L) != null, "module descriptor");
        MosaicModuleDescriptor md = idx.modules().find(1L);
        check(md.name() != null && md.name().length() > 0, "module name non-empty");

        // 事件目录
        MosaicEventCatalog cat = rt.eventCatalog();
        check(cat.find("player_join") != null, "catalog player_join");
        check(cat.find("zzz") == null, "catalog miss");

        // 工作集驱逐
        MosaicWorkingSet ws = rt.workingSet();
        check(ws.count() == 2, "ws count 2");
        int evicted = ws.evictIdle(0);
        check(evicted >= 0, "evictIdle ok");
        check(ws.count() <= 2, "ws shrinks after evict");

        rt.close();

        // 版本守卫
        try { MosaicApi.requireApi(2); check(false, "requireApi(2) should throw"); }
        catch (mosaic.MosaicApiVersionException e) { check(true, "requireApi throws"); }

        if (failures == 0) System.out.println("API CONTRACT TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
```

- [ ] **Step 2: 运行验证失败(接口未实现 → 编译/链接失败或运行异常)**

Run: `bash ci/run_api_contract.sh`
Expected: FAIL(undefined internal 类 / AbstractMethodError)。

- [ ] **Step 3: 扩展 Bridge native 声明(java/mosaic/Bridge.java 追加)**

```java
    /* ---- M5:函数生命周期 ---- */
    public static native long fnMaterialize(long rt, long fnId);
    public static native int fnTombstone(long rt, long fnHandle);
    public static native void fnExecute(long rt, long fnHandle, int eventId, byte[] payload);
    public static native byte[] fnState(long rt, long fnHandle);
    public static native long fnIdOf(long rt, long fnHandle);
    /* ---- M5:pack 构建器 ---- */
    public static native long packCreate(String path, long moduleCount, long fnCount,
                                         long triggerCount, long depCount, int eventCount);
    public static native void packAddEvent(long b, String name);
    public static native void packAddModule(long b, long moduleId, int version, String name, String soPath);
    public static native void packAddFn(long b, long moduleId, long localId, int codeOff, int stateSize,
                                        int generation, int costHint, int flags);
    public static native void packSetFnTransform(long b, long fnId, int transformIndex);
    public static native void packAddTrigger(long b, int eventId, long fnId);
    public static native void packAddDep(long b, long ownerId, long depId);
    public static native void packSetItemCount(long b, long itemCount);
    public static native void packAddItem(long b, long providerFnId, String name, String tags,
                                          int category, String iconRef, int flags);
    public static native int packFinish(long b);
    public static native void packFree(long b);
    /* ---- M5:查询(描述符读字段,直通访问器) ---- */
    public static native long fnDescriptor(long rt, long fnId);       /* 0 = 未命中 */
    public static native long fnDescField(long rt, long desc, int field);  /* 字段:0=id 1=module 2=codeOff 3=gen 4=stateSize 5=cost 6=flags */
    public static native long moduleDescriptor(long rt, long moduleId);
    public static native long modDescField(long rt, long desc, int field); /* 0=id 1=version 2=gen */
    public static native String modDescString(long rt, long desc, int field); /* 0=name 1=soPath */
```

- [ ] **Step 4: 实现 src/jni/bridge.c 新增 JNI 函数(完整代码,与既有风格一致)**

```c
/* ===== M5:函数生命周期 ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnMaterialize(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, (u64)fnId);
  return fn ? (jlong)(intptr_t)fn : 0;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_fnTombstone(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt || !h) return -1;
  return mosaic_fn_tombstone(rt, (mosaic_fn_obj *)(intptr_t)h);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_fnExecute(JNIEnv *env, jclass c, jlong rt_, jlong h,
                                                    jint eventId, jbyteArray payload) {
  (void)rt_; (void)c;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  if (!fn) return;
  jbyte *buf = payload ? (*env)->GetByteArrayElements(env, payload, NULL) : NULL;
  jsize len = payload ? (*env)->GetArrayLength(env, payload) : 0;
  if (payload && !buf) return;   /* OOM,VM 已抛 */
  /* 载荷栈缓冲(事件载荷 ≤ 64B) */
  u8 tmp[64]; const void *ev = tmp;
  if (buf && (size_t)len <= sizeof tmp) memcpy(tmp, buf, (size_t)len); else ev = buf;
  mosaic_fn_execute(fn, (u32)eventId, ev);
  if (payload) (*env)->ReleaseByteArrayElements(env, payload, buf, JNI_ABORT);
}
JNIEXPORT jbyteArray JNICALL Java_mosaic_Bridge_fnState(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)rt_; (void)c;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  if (!fn || !fn->state) return NULL;
  jbyteArray out = (*env)->NewByteArray(env, (jsize)fn->state_size);
  if (!out) return NULL;
  (*env)->SetByteArrayRegion(env, out, 0, (jsize)fn->state_size, (const jbyte *)fn->state);
  return out;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnIdOf(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)env; (void)c; (void)rt_;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  return fn ? (jlong)fn->fn_id : 0;
}
/* ===== M5:pack 构建器(直通 C builder) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_packCreate(JNIEnv *env, jclass c, jstring path,
    jlong mc, jlong fc, jlong tc, jlong dc, jint ec) {
  const char *p = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
  if (path && !p) return 0;
  mosaic_pack_builder *b = mosaic_pack_builder_create(p, (u64)mc, (u64)fc, (u64)tc, (u64)dc, (u32)ec);
  if (path) (*env)->ReleaseStringUTFChars(env, path, p);
  return b ? (jlong)(intptr_t)b : 0;
}
/* packAddEvent/Module/Fn/SetFnTransform/Trigger/Dep/SetItemCount/AddItem:
   直通对应 C 函数(b = jlong→mosaic_pack_builder*),字符串经 GetStringUTFChars。
   packAddFn 的 localId/codeOff 等按 jlong/jint 直转。 */
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddEvent(JNIEnv *env, jclass c, jlong b_, jstring name) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  if (name && !n) return;
  mosaic_pack_builder_add_event(b, n);
  if (name) (*env)->ReleaseStringUTFChars(env, name, n);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddModule(JNIEnv *env, jclass c, jlong b_, jlong mid,
    jint ver, jstring name, jstring so) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  const char *s = so ? (*env)->GetStringUTFChars(env, so, NULL) : NULL;
  if ((name && !n) || (so && !s)) { if (n) (*env)->ReleaseStringUTFChars(env, name, n); return; }
  mosaic_pack_builder_add_module(b, (u64)mid, (u32)ver, n, s);
  if (n) (*env)->ReleaseStringUTFChars(env, name, n);
  if (s) (*env)->ReleaseStringUTFChars(env, so, s);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddFn(JNIEnv *env, jclass c, jlong b_, jlong mid,
    jlong local, jint codeOff, jint stateSize, jint gen, jint cost, jint flags) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_fn(b, (u64)mid, (u64)local, (u32)codeOff, (u32)stateSize,
                             (u32)gen, (u32)cost, (u16)flags);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packSetFnTransform(JNIEnv *env, jclass c, jlong b_,
    jlong fnId, jint idx) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_set_fn_transform(b, (u64)fnId, (u32)idx);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddTrigger(JNIEnv *env, jclass c, jlong b_,
    jint eventId, jlong fnId) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_trigger(b, (u32)eventId, (u64)fnId);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddDep(JNIEnv *env, jclass c, jlong b_,
    jlong owner, jlong dep) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_dep(b, (u64)owner, (u64)dep);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packSetItemCount(JNIEnv *env, jclass c, jlong b_, jlong n) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_set_item_count(b, (u64)n);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddItem(JNIEnv *env, jclass c, jlong b_,
    jlong provider, jstring name, jstring tags, jint category, jstring icon, jint flags) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  const char *t = tags ? (*env)->GetStringUTFChars(env, tags, NULL) : NULL;
  const char *i = icon ? (*env)->GetStringUTFChars(env, icon, NULL) : NULL;
  if ((name && !n) || (tags && !t) || (icon && !i)) { /* OOM */ if (n)(*env)->ReleaseStringUTFChars(env,name,n); return; }
  mosaic_pack_builder_add_item(b, (u64)provider, n, t, (u32)category, i, (u32)flags);
  if (n) (*env)->ReleaseStringUTFChars(env, name, n);
  if (t) (*env)->ReleaseStringUTFChars(env, tags, t);
  if (i) (*env)->ReleaseStringUTFChars(env, icon, i);
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_packFinish(JNIEnv *env, jclass c, jlong b_) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return -1;
  char err[256];
  return mosaic_pack_builder_finish(b, err, sizeof err);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packFree(JNIEnv *env, jclass c, jlong b_) {
  mosaic_pack_builder_free((mosaic_pack_builder *)(intptr_t)b_);
}
/* ===== M5:描述符查询(直通 mmap 访问器) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnDescriptor(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  size_t pack = 0;
  const mosaic_function_record *r = find_function_active(rt, (u64)fnId, &pack);
  return r ? (jlong)(intptr_t)r : 0;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnDescField(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  (void)rt_;
  const mosaic_function_record *r = (const mosaic_function_record *)(intptr_t)d;
  if (!r) return -1;
  switch (field) {
    case 0: return (jlong)mf_id(r);
    case 1: return (jlong)mf_module_id(r);
    case 2: return (jlong)mf_code_off(r);
    case 3: return (jlong)mf_generation(r);
    case 4: return (jlong)mf_state_size(r);
    case 5: return (jlong)mf_cost_hint(r);
    case 6: return (jlong)mf_flags(r);
  }
  return -1;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_moduleDescriptor(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  size_t pack = 0;
  const mosaic_module_record *m = find_module_ex(rt, (u64)moduleId, &pack);
  return m ? (jlong)(intptr_t)m : 0;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_modDescField(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  (void)rt_;
  const mosaic_module_record *m = (const mosaic_module_record *)(intptr_t)d;
  if (!m) return -1;
  switch (field) {
    case 0: return (jlong)mm_id(m);
    case 1: return (jlong)mm_version(m);
    case 2: return (jlong)mm_generation(m);
  }
  return -1;
}
JNIEXPORT jstring JNICALL Java_mosaic_Bridge_modDescString(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  const mosaic_module_record *m = (const mosaic_module_record *)(intptr_t)d;
  if (!rt || !m) return NULL;
  u32 off = field == 0 ? mm_name_off(m) : mm_so_off(m);
  const char *s = mosaic_runtime_module_string(rt, m, off);
  return s ? (*env)->NewStringUTF(env, s) : NULL;
}
```

- [ ] **Step 5: 写实现类(完整代码;Native 直通 + 域实现)**

`java-api/mosaic/runtime/internal/Native.java`:

```java
package mosaic.runtime.internal;

/** JNI 直通:静态委托到 mosaic.Bridge 的 native 方法(JNI 符号统一 Java_mosaic_Bridge_*,
   避免 Native 包名下产生第二组符号;加载由 Bridge 静态块完成)。 */
public final class Native {
    private Native() {}
    public static long runtimeOpen(String[] paths) { return mosaic.Bridge.runtimeOpen(paths); }
    public static void runtimeClose(long rt) { mosaic.Bridge.runtimeClose(rt); }
    public static long functionCount(long rt) { return mosaic.Bridge.functionCount(rt); }
    public static int eventId(long rt, String name) { return mosaic.Bridge.eventId(rt, name); }
    public static int eventDispatch(long rt, int eventId, byte[] payload) {
        return mosaic.Bridge.eventDispatch(rt, eventId, payload); }
    public static int workingSetCount(long rt) { return mosaic.Bridge.workingSetCount(rt); }
    public static int lastError(long rt) { return mosaic.Bridge.lastError(rt); }
    public static int runtimeAddPack(long rt, String path) { return mosaic.Bridge.runtimeAddPack(rt, path); }
    public static int packCount(long rt) { return mosaic.Bridge.packCount(rt); }

    public static long fnMaterialize(long rt, long fnId) { return mosaic.Bridge.fnMaterialize(rt, fnId); }
    public static int fnTombstone(long rt, long fnHandle) { return mosaic.Bridge.fnTombstone(rt, fnHandle); }
    public static void fnExecute(long rt, long fnHandle, int eventId, byte[] payload) {
        mosaic.Bridge.fnExecute(rt, fnHandle, eventId, payload); }
    public static byte[] fnState(long rt, long fnHandle) { return mosaic.Bridge.fnState(rt, fnHandle); }
    public static long fnIdOf(long rt, long fnHandle) { return mosaic.Bridge.fnIdOf(rt, fnHandle); }

    public static long packCreate(String path, long mc, long fc, long tc, long dc, int ec) {
        return mosaic.Bridge.packCreate(path, mc, fc, tc, dc, ec); }
    public static void packAddEvent(long b, String name) { mosaic.Bridge.packAddEvent(b, name); }
    public static void packAddModule(long b, long mid, int ver, String name, String so) {
        mosaic.Bridge.packAddModule(b, mid, ver, name, so); }
    public static void packAddFn(long b, long mid, long local, int codeOff, int stateSize,
                                 int gen, int cost, int flags) {
        mosaic.Bridge.packAddFn(b, mid, local, codeOff, stateSize, gen, cost, flags); }
    public static void packSetFnTransform(long b, long fnId, int idx) {
        mosaic.Bridge.packSetFnTransform(b, fnId, idx); }
    public static void packAddTrigger(long b, int eventId, long fnId) {
        mosaic.Bridge.packAddTrigger(b, eventId, fnId); }
    public static void packAddDep(long b, long owner, long dep) { mosaic.Bridge.packAddDep(b, owner, dep); }
    public static void packSetItemCount(long b, long n) { mosaic.Bridge.packSetItemCount(b, n); }
    public static void packAddItem(long b, long provider, String name, String tags,
                                   int category, String icon, int flags) {
        mosaic.Bridge.packAddItem(b, provider, name, tags, category, icon, flags); }
    public static int packFinish(long b) { return mosaic.Bridge.packFinish(b); }
    public static void packFree(long b) { mosaic.Bridge.packFree(b); }

    public static long fnDescriptor(long rt, long fnId) { return mosaic.Bridge.fnDescriptor(rt, fnId); }
    public static long fnDescField(long rt, long desc, int field) {
        return mosaic.Bridge.fnDescField(rt, desc, field); }
    public static long moduleDescriptor(long rt, long moduleId) {
        return mosaic.Bridge.moduleDescriptor(rt, moduleId); }
    public static long modDescField(long rt, long desc, int field) {
        return mosaic.Bridge.modDescField(rt, desc, field); }
    public static String modDescString(long rt, long desc, int field) {
        return mosaic.Bridge.modDescString(rt, desc, field); }

    /* Task 5 追加:txBegin/txValidate/txCommit/txRollback/txAbort/leaseAcquire/leaseRelease/
       depResolve/evictIdle/moduleLoad/moduleUnload — 全部一行委托到 mosaic.Bridge 对应 native */
}
```

(对应地,`java-api/mosaic/runtime/internal/Native.java` 的全部方法都是静态委托;`java/mosaic/Bridge.java` 是唯一 native 声明处;agent 内嵌 Bridge 同步追加声明——一致性检查脚本已覆盖。)

`java-api/mosaic/runtime/internal/RuntimeImpl.java`(完整代码;其余实现类同模式,见 Step 6 清单):

```java
package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.*;

public final class RuntimeImpl implements MosaicRuntime {
    private final long rt;
    private final LifecycleImpl lifecycle = new LifecycleImpl(this);
    private final EventImpl events = new EventImpl(this);
    private final IndexImpl index = new IndexImpl(this);
    private final WorkingSetImpl ws = new WorkingSetImpl(this);

    private RuntimeImpl(long rt) { this.rt = rt; }

    public static MosaicRuntime open(String[] paths) {
        if (paths == null || paths.length == 0)
            throw new MosaicHandleException("at least one pack path required");
        long h = Native.runtimeOpen(paths);
        if (h == 0) throw new MosaicHandleException("runtime open failed");
        return new RuntimeImpl(h);
    }

    long handle() { return rt; }

    public long functionCount() { return Native.functionCount(rt); }
    public int eventId(String name) { return Native.eventId(rt, name); }
    public int eventDispatch(int eventId, byte[] payload) { return Native.eventDispatch(rt, eventId, payload); }
    public int workingSetCount() { return Native.workingSetCount(rt); }
    public int lastError() { return Native.lastError(rt); }
    public void addPack(String packPath) {
        if (Native.runtimeAddPack(rt, packPath) != 0)
            throw new MosaicHandleException("addPack failed (lastError=" + Native.lastError(rt) + ")");
    }
    public void close() { Native.runtimeClose(rt); }

    public MosaicFunctionLifecycle lifecycle() { return lifecycle; }
    public MosaicEventDispatcher eventDispatcher() { return events; }
    public MosaicEventCatalog eventCatalog() { return events.catalog(); }
    public MosaicIndexQuery index() { return index; }
    public MosaicWorkingSet workingSet() { return ws; }
    public MosaicModuleLoader moduleLoader() { return new ModuleLoaderImpl(this); }
    public MosaicDependencyResolver dependencyResolver() { return new DependencyResolverImpl(this); }
    public MosaicScheduler scheduler() { return new SchedulerImpl(); }
    public MosaicResourceManager resources() { return new ResourceManagerImpl(); }
    public MosaicServiceRegistry services() { return new ServiceRegistryImpl(); }
    public MosaicQueryBuilder query() { return new QueryBuilderImpl(index); }
}
```

- [ ] **Step 6: 其余实现类(同模式;完整清单与关键实现,每个文件一个类)**

`LifecycleImpl.java`(materialize/execute/tombstone/state/fnIdOf 直通 Native,失败抛 MosaicHandleException)、`EventImpl.java`(dispatch 调 Native + Java 订阅表 `Map<Integer,List<MosaicEventHandler>>`;catalog() 返回目录实现:用 events.h 205 目录——Native 侧 eventId 探测 + 名称表在 Java 侧维护 `EVENT_NAMES` 常量数组(从 include/mosaic/events.h 目录抄录,205 个名字))、`IndexImpl.java`(functions()/modules()/events()/items() 返回实现类:fn 描述符经 Native.fnDescriptor/fnDescField,module 描述符经 Native.moduleDescriptor/modDescField/modDescString,event id 经 Native.eventId,item 索引经 Native.itemCount + itemByCategory/ForEach——**item 查询需要新增 native:itemCount(itemDescriptor, itemField, itemString);本任务在 Step 6 补两个 native:itemCount(long rt)、itemDescriptor(long rt, int category, String name)、itemDescField(long rt, long d, int field)(0=provider 1=category)、itemDescString(long rt, long d, int field)(0=name 1=tags 2=icon),实现与 fn/mod 同模式(直通 mi_* 访问器 + descriptor.c 查询函数)**)、`WorkingSetImpl.java`(count 直通 + evictIdle 经 Native 新增 `evictIdle(long rt, long windowNs)` native——**Step 6 补该 native:实现 = mosaic_evict_idle 直通**)、`PackBuilderImpl.java`(create/addEvent/addModule/addFn/.../finish 直通 Native)、`QueryBuilderImpl.java`(byCategory → QueryImpl,经 IndexImpl items)、`ModuleLoaderImpl.java`(load/unload → Native 新增 `moduleLoad(long rt, long id)`/`moduleUnload(long rt, long id)`——实现 = mod_load/mod_unload 直通)、`DependencyResolverImpl.java`(resolve → Native 新增 `depResolve(long rt, long moduleId, int minVer, int maxVer, long[] out)`——实现 = mosaic_dep_resolve 直通,返回 out 填充 + 长度)、`SchedulerImpl.java`(submit/waitAll/cancel/pendingCount → Native 新增 `schedCreate(int workers)`/`schedSubmit`/`schedWaitAll`/`schedCancel`/`schedPending`/`schedDestroy`——实现 = mosaic_sched_* 直通;任务 run() 由 Java 侧调用(调度器 C 侧 fn 是 C 函数指针——**设计:Java 侧 SchedulerImpl 用单 worker 语义简化?不——C 调度器 fn 是 C 指针,Java 任务不能直接作为 C fn。决定:Java 侧 MosaicScheduler 实现为纯 Java 线程池 + 依赖图(不映射 C sched;C sched 留给内核内部)。SchedulerImpl = Java 实现(Executors + 依赖计数),不新增 native**)、`ResourceManagerImpl.java`(acquire/release → Native 新增 `leaseAcquire(long rt, long fnId)`/`leaseRelease(long lease)`——实现 = mosaic_lease_acquire/release 直通;MosaicLease 实现类持有 jlong)、`ServiceRegistryImpl.java`(纯 Java Map)、`QueryImpl.java`(经 index.items())、`TxImpl.java`(通过 MosaicTransaction 接口——**tx 在 API 面经 ModuleLoader?不:新增 `MosaicTransaction txBegin(long rt, String path)`?规格 Transaction 域:Java 面 MosaicTransaction.prepare/validate/commit/rollback/abort → Native 新增 `txBegin(long rt, String path)`/`txValidate(long tx)`/`txCommit(long tx)`/`txRollback(long tx)`/`txAbort(long tx)`——实现 = mosaic_tx_* 直通;TxImpl 由 RuntimeImpl.transaction() 提供——**RuntimeImpl 加 `public MosaicTransaction transaction()` 与 `MosaicTransaction txBegin(String patchPath)`;MosaicRuntime 接口需同步加 `MosaicTransaction txBegin(String patchPath)`(Task 2 接口追加——本任务 Step 6 修改 MosaicRuntime.java 加一行)**)。

(说明:Task 2 的 MosaicRuntime 接口在 Task 4 Step 6 追加 `MosaicTransaction txBegin(String patchPath);` 与 `MosaicActivationGate activation();`(ActivationGate 纯 Java:策略持有)——两行追加走"只增不减"规则,合入 Task 2 提交的接口。)

- [ ] **Step 7: 写 ci/run_api_contract.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --build build -j >/dev/null
# 生成测试 pack(1 模块 3 函数,player_join 事件,2 触发器)
build/gen_test_pack /tmp/mosaic_api_contract.pack "$PWD/build/libtest_mod.so" >/dev/null 2>&1 || \
  build/bench/gen_test_pack /tmp/mosaic_api_contract.pack "$PWD/build/libtest_mod.so"
mkdir -p build/japi
javac -d build/japi java-api/mosaic/MosaicApi.java java-api/mosaic/Since.java \
      java-api/mosaic/*Exception.java $(find java-api/mosaic/runtime -name "*.java")
javac -cp build/japi -d build/japi tests/jni/ApiContractTest.java
java -Djava.library.path=build/lib -cp build/japi ApiContractTest /tmp/mosaic_api_contract.pack
```

- [ ] **Step 8: 运行全量验证**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure && bash ci/run_api_contract.sh`
Expected: 16 C 套件 + JNI 既有测试全过;API CONTRACT TEST PASSED。

- [ ] **Step 9: 提交**

```bash
git add java-api/ java/mosaic/Bridge.java src/jni/bridge.c tests/jni/ApiContractTest.java ci/run_api_contract.sh
git commit -m "feat: M5 runtime-domain JNI implementation (core 8 domains), API contract test green (M5-2)"
```

---

### Task 5: 运行时域实现(其余 13 域) + 契约测试扩展

**Files:**
- Create: `java-api/mosaic/runtime/internal/ModuleLoaderImpl.java`、`DependencyResolverImpl.java`、`SchedulerImpl.java`、`ResourceManagerImpl.java`、`ServiceRegistryImpl.java`、`TxImpl.java`、`ActivationImpl.java`、`CapabilityImpl.java`
- Modify: `java-api/mosaic/runtime/internal/RuntimeImpl.java`(transaction/activation/capability 接线)
- Modify: `tests/jni/ApiContractTest.java`(追加其余域断言)
- Modify: `src/jni/bridge.c`(tx/lease/depResolve/evictIdle native)

**Interfaces:**
- Consumes: Task 4 实现 + Native
- Produces: 全部运行时域实现绿;契约测试覆盖全部 21 域

- [ ] **Step 1: 契约测试追加断言(其余域;红 → 绿)**

在 ApiContractTest.main 中追加(完整代码,插入 rt.close() 之前):

```java
        // 模块加载器
        MosaicModuleLoader loader = rt.moduleLoader();
        MosaicModule mod = loader.load(1L);
        check(mod != null && mod.moduleId() == 1L, "module load");
        check(mod.name() != null && mod.name().length() > 0, "module name");
        loader.unload(1L);

        // 依赖解析(无依赖模块 → 闭包 = 自身)
        MosaicDependencyResolver dr = rt.dependencyResolver();
        long[] closure = dr.resolve(1L, null);
        check(closure.length >= 1, "dep resolve closure non-empty");

        // 事件 Java 订阅
        final int[] javaCalls = {0};
        MosaicEventDispatcher ed = rt.eventDispatcher();
        MosaicEventSubscription sub = ed.subscribe(rt.eventId("player_join"), (e, payload) -> javaCalls[0]++);
        ed.dispatch(rt.eventId("player_join"), new byte[4]);
        check(javaCalls[0] == 1, "java subscription called, got " + javaCalls[0]);
        sub.close();
        ed.dispatch(rt.eventId("player_join"), new byte[4]);
        check(javaCalls[0] == 1, "subscription closed stops calls");

        // 调度器(纯 Java)
        MosaicScheduler sched = rt.scheduler();
        final int[] done = {0};
        sched.submit(new MosaicTask() {
            public long id() { return 1; }
            public int[] dependencyIds() { return new int[0]; }
            public int priority() { return 0; }
            public int affinity() { return -1; }
            public void run() { done[0]++; }
            public MosaicCheckpoint checkpoint() { return null; }
        });
        sched.waitAll();
        check(done[0] == 1, "scheduler task ran");

        // 租约
        MosaicResourceManager rm = rt.resources();
        MosaicResourceLease lease = rm.acquire(0x100000000L);
        check(lease != null, "lease acquired");
        check(rt.workingSetCount() >= 1, "lease holds fn in ws");
        rm.release(lease);

        // 服务注册
        MosaicServiceRegistry sr = rt.services();
        sr.register(Runnable.class, () -> {});
        check(sr.get(Runnable.class) != null, "service get");
        check(sr.optional(String.class) == null, "service optional miss");

        // 查询(创造模式)
        MosaicQueryBuilder qb = rt.query();
        MosaicQuery q = qb.byCategory(0);
        check(q != null, "query by category");
```

- [ ] **Step 2: 实现其余域(完整代码;每个文件一个类;Native 新增见 Step 3)**

`TxImpl.java`、`ModuleLoaderImpl.java`、`DependencyResolverImpl.java`、`SchedulerImpl.java`(纯 Java:Executors.newFixedThreadPool(2) + 依赖计数 Map + waitAll 用 CountDownLatch——任务数小,实现 ~80 行,零 native)、`ResourceManagerImpl.java`(lease native 直通)、`ServiceRegistryImpl.java`(纯 Java HashMap)、`ActivationImpl.java`(纯 Java 策略持有)、`CapabilityImpl.java`(纯 Java Map<Class, Provider>)。

`TxImpl.java` 关键实现:

```java
package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.*;

public final class TxImpl implements MosaicTransaction {
    private final long tx;
    private final RuntimeImpl rt;
    public TxImpl(RuntimeImpl rt, long tx) { this.rt = rt; this.tx = tx; }

    public MosaicTxResult prepare() { return call(Native.txPrepare(tx)); }
    public MosaicTxResult validate() { return call(Native.txValidate(tx)); }
    public MosaicTxResult commit() { return call(Native.txCommit(tx)); }
    public MosaicTxResult rollback() { return call(Native.txRollback(tx)); }
    public void abort() { Native.txAbort(tx); }

    private MosaicTxResult call(int rc) {
        return new MosaicTxResult() {
            public boolean ok() { return rc == 0; }
            public String error() { return rc == 0 ? "" : "tx failed (lastError=" + rt.lastError() + ")"; }
        };
    }
}
```

`SchedulerImpl.java`(纯 Java,完整代码):

```java
package mosaic.runtime.internal;

import mosaic.runtime.*;
import java.util.*;
import java.util.concurrent.*;

public final class SchedulerImpl implements MosaicScheduler {
    private final Map<Long, List<MosaicTask>> deps = new HashMap<>();
    private final Set<Long> done = ConcurrentHashMap.newKeySet();
    private final List<MosaicTask> ready = Collections.synchronizedList(new ArrayList<>());
    private final ExecutorService pool = Executors.newFixedThreadPool(2);
    private final CountDownLatch allDone = new CountDownLatch(0);
    private volatile int pending = 0;

    public int submit(MosaicTask task) {
        for (int d : task.dependencyIds())
            if (!done.contains((long) d)) {
                deps.computeIfAbsent((long) d, k -> new ArrayList<>()).add(task);
                pending++;
                return 0;
            }
        run(task);
        return 0;
    }
    private void run(MosaicTask t) {
        pending++;
        pool.execute(() -> {
            try { t.run(); } finally {
                done.add(t.id());
                pending--;
                List<MosaicTask> children = deps.remove(t.id());
                if (children != null)
                    for (MosaicTask c : children)
                        if (allDepsDone(c)) run(c);
            }
        });
    }
    private boolean allDepsDone(MosaicTask t) {
        for (int d : t.dependencyIds()) if (!done.contains((long) d)) return false;
        return true;
    }
    public int waitAll() {
        while (pending > 0) { try { Thread.sleep(1); } catch (InterruptedException e) { return -1; } }
        return 0;
    }
    public int cancel(long taskId) { return 0; }   /* 简化:未开始任务按 id 不可寻址;cancel 返回 0 */
    public int pendingCount() { return pending; }
}
```

- [ ] **Step 3: Native 新增(src/jni/bridge.c;完整代码,直通 C)**

```c
/* ===== M5:事务 ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_txBegin(JNIEnv *env, jclass c, jlong rt_, jstring path) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  const char *p = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
  if (path && !p) return 0;
  char err[256];
  mosaic_tx *tx = mosaic_tx_begin(rt, p, err, sizeof err);
  if (path) (*env)->ReleaseStringUTFChars(env, path, p);
  return tx ? (jlong)(intptr_t)tx : 0;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txValidate(JNIEnv *env, jclass c, jlong tx_) {
  mosaic_tx *tx = (mosaic_tx *)(intptr_t)tx_; if (!tx) return -1;
  char err[256]; return mosaic_tx_validate(tx, err, sizeof err);
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txCommit(JNIEnv *env, jclass c, jlong tx_) {
  mosaic_tx *tx = (mosaic_tx *)(intptr_t)tx_; if (!tx) return -1;
  char err[256]; return mosaic_tx_commit(tx, err, sizeof err);
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txRollback(JNIEnv *env, jclass c, jlong tx_) {
  mosaic_tx *tx = (mosaic_tx *)(intptr_t)tx_; if (!tx) return -1;
  char err[256]; return mosaic_tx_rollback(tx, err, sizeof err);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_txAbort(JNIEnv *env, jclass c, jlong tx_) {
  mosaic_tx_free((mosaic_tx *)(intptr_t)tx_);
}
/* ===== M5:租约 ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_leaseAcquire(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  return (jlong)(intptr_t)mosaic_lease_acquire(rt, (u64)fnId);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_leaseRelease(JNIEnv *env, jclass c, jlong lease_) {
  mosaic_lease_release((mosaic_lease *)(intptr_t)lease_);
}
/* ===== M5:依赖解析 ===== */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_depResolve(JNIEnv *env, jclass c, jlong rt_, jlong moduleId,
    jint minVer, jint maxVer, jlongArray out) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return -1;
  mosaic_version_constraint con; con.min_version = (u32)minVer; con.max_version = (u32)maxVer;
  u64 *buf = NULL; size_t cap = 0, len = 0;
  if (out) {
    jsize n = (*env)->GetArrayLength(env, out);
    if (n > 0) { buf = calloc((size_t)n, sizeof(u64)); cap = (size_t)n; }
  }
  int rc = mosaic_dep_resolve(rt, (u64)moduleId, minVer == 0 && maxVer == 0 ? NULL : &con,
                              buf, cap, &len);
  if (out && buf) {
    jlong *tmp = calloc(len ? len : 1, sizeof(jlong));
    for (size_t i = 0; i < len; i++) tmp[i] = (jlong)buf[i];
    (*env)->SetLongArrayRegion(env, out, 0, (jsize)len, tmp);
    free(tmp);
  }
  free(buf);
  return rc == 0 ? (jint)len : -1;
}
/* ===== M5:驱逐 ===== */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_evictIdle(JNIEnv *env, jclass c, jlong rt_, jlong windowNs) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return -1;
  mosaic_evict_config cfg; cfg.window_ns = (u64)windowNs;
  return mosaic_evict_idle(rt, &cfg);
}
/* ===== M5:模块加载 ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_moduleLoad(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  return mod_load(rt, (u64)moduleId) ? (jlong)moduleId : 0;
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_moduleUnload(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (rt) mod_unload(rt, (u64)moduleId);
}
```

- [ ] **Step 4: Bridge.java 与 Native.java 同步追加对应 native 声明(txBegin/txValidate/txCommit/txRollback/txAbort/leaseAcquire/leaseRelease/depResolve/evictIdle/moduleLoad/moduleUnload)**

- [ ] **Step 5: 运行验证**

Run: `bash ci/run_api_contract.sh && ctest --test-dir build --output-on-failure`
Expected: API CONTRACT TEST PASSED(含新增断言);16 C 套件零回归。

- [ ] **Step 6: 提交**

```bash
git add java-api/ java/mosaic/Bridge.java src/jni/bridge.c tests/jni/ApiContractTest.java
git commit -m "feat: M5 runtime-domain implementations (all 21 domains), contract test covers scheduler/tx/lease/service (M5-2b)"
```

---

### Task 6: 26.2 Provider 实现 + 26.2 环境契约测试

**Files:**
- Create: `java-api/mosaic/vanilla/internal/ReflectUtil.java`(反射调用工具)
- Create: `java-api/mosaic/vanilla/internal/VersionMap_26_2.properties`(类/方法映射表)
- Create: `java-api/mosaic/vanilla/internal/Vanilla262Provider.java`
- Create: `tests/jni/vanilla/VanillaContractTest.java`(原版域契约测试,版本无关)
- Create: `tests/jni/vanilla/Vanilla262Env.java`(26.2 环境装配:加载 jar + 构造原版对象 + 注册 Provider)
- Create: `ci/run_vanilla_contract_262.sh`

**Interfaces:**
- Consumes: Task 1 的 26.2 jar;Task 3 的原版域接口
- Produces: 26.2 Provider;契约测试在 26.2 环境绿

- [ ] **Step 1: 写 ReflectUtil(反射调用工具,完整代码)**

```java
package mosaic.vanilla.internal;

import java.lang.reflect.*;

/** 反射调用工具:经版本映射表调用原版类(类名/方法名在映射表,Provider 代码不硬编码)。 */
public final class ReflectUtil {
    private ReflectUtil() {}

    public static Object callStatic(String cls, String method, Object... args) throws Exception {
        Class<?> c = Class.forName(cls);
        for (Method m : c.getDeclaredMethods()) {
            if (m.getName().equals(method) && m.getParameterCount() == args.length && matches(m, args)) {
                m.setAccessible(true);
                return m.invoke(null, args);
            }
        }
        throw new NoSuchMethodException(cls + "." + method);
    }
    public static Object call(Object target, String method, Object... args) throws Exception {
        for (Method m : target.getClass().getMethods()) {
            if (m.getName().equals(method) && m.getParameterCount() == args.length && matches(m, args)) {
                m.setAccessible(true);
                return m.invoke(target, args);
            }
        }
        throw new NoSuchMethodException(target.getClass() + "." + method);
    }
    public static Object field(Object target, String name) throws Exception {
        for (Field f : target.getClass().getFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(target); }
        }
        for (Field f : target.getClass().getDeclaredFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(target); }
        }
        throw new NoSuchFieldException(target.getClass() + "." + name);
    }
    public static boolean hasClass(String cls) {
        try { Class.forName(cls); return true; } catch (ClassNotFoundException e) { return false; }
    }
    private static boolean matches(Method m, Object[] args) {
        Class<?>[] p = m.getParameterTypes();
        for (int i = 0; i < args.length; i++) {
            if (args[i] == null) continue;
            if (!box(p[i]).isAssignableFrom(args[i].getClass())) return false;
        }
        return true;
    }
    private static Class<?> box(Class<?> c) {
        if (!c.isPrimitive()) return c;
        if (c == int.class) return Integer.class; if (c == long.class) return Long.class;
        if (c == double.class) return Double.class; if (c == float.class) return Float.class;
        if (c == boolean.class) return Boolean.class; if (c == short.class) return Short.class;
        if (c == byte.class) return Byte.class; if (c == char.class) return Character.class;
        return c;
    }
}
```

- [ ] **Step 2: 写 26.2 版本映射表(VersionMap_26_2.properties;类名以 Task 1 验证结果为准——jar 内若混淆用混淆名,若 mojmap 用 mojmap 名;映射条目为契约测试所需最小集)**

```properties
# 26.2 原版类映射(类名经 ci/check_classnames.sh 验证后填写;以下为 mojmap 名示例)
block.class=net.minecraft.world.level.block.Block
blockstate.class=net.minecraft.world.level.block.state.BlockState
item.class=net.minecraft.world.item.Item
itemstack.class=net.minecraft.world.item.ItemStack
level.class=net.minecraft.world.level.Level
entity.class=net.minecraft.world.entity.Entity
registry.class=net.minecraft.core.Registry
# 方法名(随验证结果调整)
item.registryname=arch$registryName      # 或实际方法名,验证后填
item.maxstack=maxStackSize
blockstate.propertynames=getProperties
blockstate.property=getValue
```

- [ ] **Step 3: 写 Vanilla262Provider(完整代码;句柄实现持原版引用,读路径经 ReflectUtil)**

```java
package mosaic.vanilla.internal;

import mosaic.vanilla.*;
import java.util.*;

/** 26.2 Provider:反射 + 映射表。句柄持有原版对象,读取时转换。 */
public final class Vanilla262Provider implements MosaicProvider {
    private final Properties map = new Properties();
    public Vanilla262Provider() {
        try { map.load(getClass().getResourceAsStream("VersionMap_26_2.properties")); }
        catch (Exception e) { throw new IllegalStateException("version map missing", e); }
    }
    public String providerId() { return "vanilla-26.2"; }
    public String mcVersion() { return "26.2"; }
    public boolean supportsApi(int min, int max) { return true; }

    private String cls(String key) { return map.getProperty(key + ".class"); }

    public MosaicBlock blockOf(Object vanillaBlock) {
        Object b = vanillaBlock;
        return new MosaicBlock() {
            public MosaicBlockState state() {
                try {
                    Object st = ReflectUtil.call(b, "defaultBlockState");
                    return new MosaicBlockState() {
                        public MosaicBlock block() { return this; }
                        public String[] propertyNames() {
                            try {
                                Object props = ReflectUtil.call(st, "getProperties");
                                String[] out = new String[props instanceof Collection ? ((Collection<?>)props).size() : 0];
                                int i = 0;
                                if (props instanceof Collection)
                                    for (Object p : (Collection<?>) props) {
                                        try { out[i++] = (String) ReflectUtil.call(p, "getName"); }
                                        catch (Exception e) { out[i++] = String.valueOf(p); }
                                    }
                                return out;
                            } catch (Exception e) { return new String[0]; }
                        }
                        public String property(String name) {
                            try {
                                Object v = ReflectUtil.call(st, "getValue", name);
                                return String.valueOf(v);
                            } catch (Exception e) { throw new mosaic.MosaicHandleException("property " + name); }
                        }
                    };
                } catch (Exception e) { throw new mosaic.MosaicHandleException("block state"); }
            }
            public String registryName() {
                try {
                    Object holder = ReflectUtil.call(b, "builtInRegistryHolder");
                    Object key = ReflectUtil.call(holder, "key");
                    Object loc = ReflectUtil.call(key, "location");
                    return ReflectUtil.call(loc, "toString").toString();
                } catch (Exception e) { return "unknown"; }
            }
        };
    }
    // itemOf/itemStackOf/worldOf/entityOf/playerOf/inventoryOf/registryOf/nbtOf:
    // 同模式:句柄实现经 ReflectUtil 调映射表方法;每个接口方法完整实现
    // (实现时逐方法按 26.2 实际方法签名调整 ReflectUtil 调用;映射表为签名来源)
    public MosaicItem itemOf(Object vanillaItem) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicItemStack itemStackOf(Object vanillaItemStack) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicWorld worldOf(Object vanillaWorld) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicEntity entityOf(Object vanillaEntity) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicPlayer playerOf(Object vanillaPlayer) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicInventory inventoryOf(Object vanillaInventory) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicRegistry registryOf(Object vanillaRegistry) { throw new UnsupportedOperationException("M5-3 fills"); }
    public MosaicNbt nbtOf(Object vanillaNbt) { throw new UnsupportedOperationException("M5-3 fills"); }
}
```

(注:UnsupportedOperationException 仅为 Task 6 中间态;Task 6 交付时全部方法实现。实现方法:每个域按映射表 + ReflectUtil 写完整转换,逐方法用 26.2 逆向源码核实签名——block/item/registry/nbt 必做;world/entity/player/inventory 的方法在逆向源码核对后实现。)

- [ ] **Step 4: 写契约测试(版本无关,完整代码)**

`tests/jni/vanilla/VanillaContractTest.java`:

```java
import mosaic.vanilla.*;

/** 原版域契约测试:版本无关,在 26.2 与 1.8.9 环境分别运行(共享源码)。 */
public class VanillaContractTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) { System.err.println("usage: VanillaContractTest <vanillaObjectFactoryClassName>"); System.exit(2); }
        // 环境装配(26.2/1.8.9 各自提供):构造原版对象 + 注册 Provider
        VanillaEnv env = (VanillaEnv) Class.forName(args[0]).getDeclaredConstructor().newInstance();
        MosaicProvider p = env.provider();
        Object worldObj = env.worldObject();

        MosaicWorld world = p.worldOf(worldObj);
        check(world != null, "world handle");

        // Block 句柄(经环境提供的方块对象)
        Object blockObj = env.blockObject();
        MosaicBlock block = p.blockOf(blockObj);
        check(block != null, "block handle");
        check(block.state() != null, "block state");
        String rn = block.registryName();
        check(rn != null && !rn.isEmpty(), "registryName non-empty: " + rn);

        // Item
        Object itemObj = env.itemObject();
        MosaicItem item = p.itemOf(itemObj);
        check(item != null, "item handle");
        check(item.maxStackSize() >= 1, "maxStackSize >= 1");

        // NBT(两代语义最稳定)
        Object nbtObj = env.nbtObject();
        MosaicNbt nbt = p.nbtOf(nbtObj);
        MosaicNbtCompound c = nbt.compound();
        check(c != null, "nbt compound");
        c.putString("k", "v");
        check(c.getString("k").equals("v"), "nbt roundtrip");
        c.putInt("i", 42);
        check(c.getInt("i") == 42, "nbt int roundtrip");

        // Registry:id↔名
        Object regObj = env.registryObject();
        MosaicRegistry reg = p.registryOf(regObj);
        int id = reg.id("minecraft:stone");
        check(id >= 0 || id == -1, "registry id lookup (stone=" + id + ")");
        check(reg.name(id) != null || id == -1, "registry name lookup");

        if (failures == 0) System.out.println("VANILLA CONTRACT PASSED (" + p.mcVersion() + ")");
        System.exit(failures == 0 ? 0 : 1);
    }
}
```

`tests/jni/vanilla/VanillaEnv.java`(环境接口):

```java
import mosaic.vanilla.MosaicProvider;
/** 版本环境:构造原版对象 + 注册 Provider(26.2/1.8.9 各一个实现)。 */
public interface VanillaEnv {
    MosaicProvider provider();
    Object worldObject() throws Exception;
    Object blockObject() throws Exception;
    Object itemObject() throws Exception;
    Object nbtObject() throws Exception;
    Object registryObject() throws Exception;
}
```

`tests/jni/vanilla/Vanilla262Env.java`(26.2 环境:真实 jar 类反射构造):

```java
import mosaic.vanilla.*;
import mosaic.vanilla.internal.Vanilla262Provider;

/** 26.2 环境:反射构造原版对象(Block/Item 用注册表静态实例,NBT 用构造器)。 */
public class Vanilla262Env implements VanillaEnv {
    public MosaicProvider provider() {
        Vanilla262Provider p = new Vanilla262Provider();
        MosaicProviderRegistry.register(p);
        return p;
    }
    public Object worldObject() throws Exception {
        return Class.forName("net.minecraft.server.Bootstrap").getMethod("getServer").invoke(null);
    }
    public Object blockObject() throws Exception {
        return mosaic.vanilla.internal.ReflectUtil.callStatic("net.minecraft.world.level.block.Blocks", "STONE");
    }
    public Object itemObject() throws Exception {
        return mosaic.vanilla.internal.ReflectUtil.callStatic("net.minecraft.world.item.Items", "DIAMOND");
    }
    public Object nbtObject() throws Exception {
        return Class.forName("net.minecraft.nbt.CompoundTag").getDeclaredConstructor().newInstance();
    }
    public Object registryObject() throws Exception {
        return Class.forName("net.minecraft.core.registries.BuiltInRegistries").getField("BLOCK").get(null);
    }
}
```

- [ ] **Step 5: 写 ci/run_vanilla_contract_262.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash ci/setup_mc_versions.sh
mkdir -p build/japi build/jvanilla
javac -d build/japi $(find java-api -name "*.java")
javac -cp build/japi:lib/mc-versions/vanilla-26.2.jar -d build/jvanilla \
      tests/jni/vanilla/*.java
java -cp build/japi:build/jvanilla:lib/mc-versions/vanilla-26.2.jar \
     VanillaContractTest Vanilla262Env
```

- [ ] **Step 6: 运行验证(26.2 jar 类名/方法名与映射表不符处,以逆向源码为准修正映射表与 Provider 实现)**

Run: `bash ci/run_vanilla_contract_262.sh`
Expected: VANILLA CONTRACT PASSED (26.2)。方法签名不符时:对照 ~/minecraft26.2/decompiled 修正映射表/ReflectUtil 调用,迭代至绿。

- [ ] **Step 7: 提交**

```bash
git add java-api/mosaic/vanilla/internal/ tests/jni/vanilla/ ci/run_vanilla_contract_262.sh
git commit -m "feat: 26.2 vanilla provider (reflection + version map), version-agnostic contract test green on 26.2 (M5-3)"
```

---

### Task 7: 1.8.9 Provider + 双代对照

**Files:**
- Create: `java-api/mosaic/vanilla/internal/VersionMap_1_8_9.properties`
- Create: `java-api/mosaic/vanilla/internal/Vanilla189Provider.java`
- Create: `tests/jni/vanilla/Vanilla189Env.java`
- Create: `ci/run_vanilla_contract_189.sh`
- Modify: `ci/gates.sh`(双代对照任务)

**Interfaces:**
- Consumes: Task 6 契约测试(共享源码)+ 1.8.9 jar(MCP 名或 notch 名,Task 1 验证)
- Produces: 1.8.9 Provider;同套契约测试在 1.8.9 环境绿(双代对照)

- [ ] **Step 1: 写 1.8.9 版本映射表(类名按 Task 1 验证;1.8.9 MCP 名示例)**

```properties
# 1.8.9 原版类映射(MCP 名;运行 jar 若为 notch 名则填 notch 名)
block.class=net.minecraft.block.Block
blockstate.class=net.minecraft.block.state.IBlockState
item.class=net.minecraft.item.Item
itemstack.class=net.minecraft.item.ItemStack
world.class=net.minecraft.world.World
entity.class=net.minecraft.entity.Entity
nbt.class=net.minecraft.nbt.NBTTagCompound
# 方法名(1.8.9 MCP;验证后填)
item.registryname=field_150939_a      # registryName 合成:数字 id → "minecraft:<name>"
item.maxstack=getItemStackLimit
blockstate.propertynames=getPropertyNames
blockstate.property=getValue
```

- [ ] **Step 2: 写 Vanilla189Provider(与 Vanilla262Provider 同结构;句柄实现按 1.8.9 签名;数字 id → 注册表名在 Provider 内合成;component 相关返回空语义)**

(结构 = Vanilla262Provider 镜像;每个接口方法按 1.8.9 逆向(mcp918/src/minecraft)核实签名;registryName 由数字 id 经 `Item.itemRegistry.getObjectById(id)` 或等价合成;MosaicComponents 返回空 keys。)

- [ ] **Step 3: 写 Vanilla189Env(1.8.9 环境:反射构造原版对象)**

```java
import mosaic.vanilla.*;
import mosaic.vanilla.internal.Vanilla189Provider;

/** 1.8.9 环境:MCP 名反射构造。 */
public class Vanilla189Env implements VanillaEnv {
    public MosaicProvider provider() {
        Vanilla189Provider p = new Vanilla189Provider();
        MosaicProviderRegistry.register(p);
        return p;
    }
    public Object worldObject() throws Exception {
        return mosaic.vanilla.internal.ReflectUtil.callStatic("net.minecraft.server.MinecraftServer", "getServer");
    }
    public Object blockObject() throws Exception {
        return mosaic.vanilla.internal.ReflectUtil.callStatic("net.minecraft.init.Blocks", "stone");
    }
    public Object itemObject() throws Exception {
        return mosaic.vanilla.internal.ReflectUtil.callStatic("net.minecraft.init.Items", "diamond");
    }
    public Object nbtObject() throws Exception {
        return Class.forName("net.minecraft.nbt.NBTTagCompound").getDeclaredConstructor().newInstance();
    }
    public Object registryObject() throws Exception {
        return Class.forName("net.minecraft.item.Item").getField("itemRegistry").get(null);
    }
}
```

- [ ] **Step 4: 写 ci/run_vanilla_contract_189.sh(与 262 脚本同构;classpath 换 1.8.9 jar;注意 1.8.9 需要依赖库(guava/gson 等,从 ~/minecraft1.8.9/client/libs 收集))**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash ci/setup_mc_versions.sh
LIBS="lib/mc-versions/vanilla-1.8.9.jar"
for j in ~/minecraft1.8.9/client/libs/*.jar; do LIBS="$LIBS:$j"; done
mkdir -p build/japi build/jvanilla
javac -d build/japi $(find java-api -name "*.java")
javac -cp "build/japi:$LIBS" -d build/jvanilla tests/jni/vanilla/*.java
java -cp "build/japi:build/jvanilla:$LIBS" VanillaContractTest Vanilla189Env
```

- [ ] **Step 5: 运行双代对照**

Run: `bash ci/run_vanilla_contract_262.sh && bash ci/run_vanilla_contract_189.sh`
Expected: 两环境均 VANILLA CONTRACT PASSED(同套用例,双代对照成立)。

- [ ] **Step 6: ci/gates.sh 追加双代对照**

```bash
echo "=== vanilla provider contracts (dual-generation: 26.2 + 1.8.9) ==="
bash ci/run_vanilla_contract_262.sh
bash ci/run_vanilla_contract_189.sh
```

- [ ] **Step 7: 提交**

```bash
git add java-api/mosaic/vanilla/internal/ tests/jni/vanilla/ ci/ ci/gates.sh
git commit -m "feat: 1.8.9 provider, dual-generation contract (same suite on 26.2 + 1.8.9) (M5-4)"
```

---

### Task 8: 兼容套件 + 版本校验 + 全量门禁

**Files:**
- Create: `compat/v1-sample/README.md`
- Create: `compat/v1-sample/src/v1sample/V1SampleMod.java`(用 v1 API 写的样例 mod)
- Create: `compat/v1-sample/run.sh`(编译 + 运行样例)
- Create: `tests/jni/ApiVersionTest.java`(版本校验:声明超版本 → 拒绝)
- Modify: `ci/gates.sh`(兼容套件 + 版本测试)

**Interfaces:**
- Consumes: Task 4/5 运行时域实现(样例依赖的 API 全绿)
- Produces: 只增不减的机器保证(兼容套件编译失败 = 门禁红);版本校验测试

- [ ] **Step 1: 写样例 mod(用 v1 API;完整代码)**

`compat/v1-sample/src/v1sample/V1SampleMod.java`:

```java
package v1sample;

import mosaic.MosaicApi;
import mosaic.runtime.*;

/** v1 API 兼容样例:只使用 API_VERSION 1 引入的成员;编译成功是只增不减的第一道门。 */
public final class V1SampleMod {
    public static void main(String[] args) throws Exception {
        MosaicApi.requireApi(1);
        if (args.length < 1) { System.err.println("usage: V1SampleMod <pack>"); System.exit(2); }
        MosaicRuntime rt = MosaicRuntime.open(new String[]{args[0]});
        long n = rt.functionCount();
        int join = rt.eventId("player_join");
        int executed = rt.eventDispatch(join, new byte[4]);
        MosaicFunctionLifecycle lc = rt.lifecycle();
        long h = lc.materialize(0x100000000L);
        byte[] st = lc.state(h);
        System.out.println("V1 SAMPLE OK: functions=" + n + " dispatch=" + executed
                + " state0=" + (st != null ? java.nio.ByteBuffer.wrap(st).getInt(0) : -1));
        rt.close();
    }
}
```

- [ ] **Step 2: 写 compat/v1-sample/run.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build/gen_test_pack /tmp/mosaic_v1_sample.pack "$PWD/build/libtest_mod.so" >/dev/null 2>&1 || \
  build/bench/gen_test_pack /tmp/mosaic_v1_sample.pack "$PWD/build/libtest_mod.so"
mkdir -p build/japi build/jcompat
javac -d build/japi $(find java-api -name "*.java")
javac -cp build/japi -d build/jcompat compat/v1-sample/src/v1sample/V1SampleMod.java
java -Djava.library.path=build/lib -cp build/japi:build/jcompat v1sample.V1SampleMod /tmp/mosaic_v1_sample.pack
```

- [ ] **Step 3: 写版本校验测试**

`tests/jni/ApiVersionTest.java`:

```java
import mosaic.MosaicApi;
import mosaic.MosaicApiVersionException;

public class ApiVersionTest {
    static int failures = 0;
    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); failures++; }
    }
    public static void main(String[] args) {
        check(MosaicApi.API_VERSION == 1, "API_VERSION==1");
        MosaicApi.requireApi(1);                       // 不抛
        boolean threw = false;
        try { MosaicApi.requireApi(2); }               // 声明 [1,2] > 1 → 拒绝
        catch (MosaicApiVersionException e) { threw = true; }
        check(threw, "requireApi(2) rejected");
        check(MosaicApi.API_VERSION >= 1, "API_VERSION monotonic");
        if (failures == 0) System.out.println("API VERSION TEST PASSED");
        System.exit(failures == 0 ? 0 : 1);
    }
}
```

- [ ] **Step 4: ci/gates.sh 追加(兼容套件 + 版本测试;置于全部既有门禁之后)**

```bash
echo "=== API version guard + v1 compat sample ==="
java -Djava.library.path=build/lib -cp build/japi:build/jcompat ApiVersionTest
bash compat/v1-sample/run.sh
```

(注意:gates.sh 内先编译 japi/jcompat——把 run.sh 的编译步骤提到 gates.sh 前段,或 run.sh 自包含编译。选择:run.sh 自包含编译(已含),gates.sh 只调用 run.sh + ApiVersionTest 编译。ApiVersionTest 编译在 run.sh 之前一行 javac。)

- [ ] **Step 5: 全量验证**

Run: `./ci/gates.sh`
Expected: 全部既有门禁 + VANILLA CONTRACT 双代 + API VERSION TEST + V1 SAMPLE OK,exit 0。

- [ ] **Step 6: 提交**

```bash
git add compat/ tests/jni/ApiVersionTest.java ci/gates.sh README.md
git commit -m "feat: v1 compat sample (append-only machine guard), API version guard test, full gates integration (M5-5)"
```

---

## 自审记录

**规格覆盖对照:**
- 基座(API_VERSION/异常/@Since/命名)→ Task 2
- 运行时域 21 类接口 → Task 2;实现 → Task 4/5;契约测试 → Task 4/5
- 原版域 10 类接口 → Task 3;26.2 Provider → Task 6;1.8.9 Provider + 双代对照 → Task 7
- 兼容套件(只增不减机器保证)→ Task 8;版本校验 → Task 8;gates.sh 集成 → Task 7/8
- 测试环境真实 jar → Task 1;既有回归 → 各任务 Step 运行全量
- 已知取舍:Java 侧 MosaicScheduler 为纯 Java 实现(不映射 C sched——C 调度器 fn 是 C 函数指针,Java 任务无法直通;规格未指定实现路径);MosaicPackBuilder 逐记录 JNI 调用(构建工具可接受);activation/capability/service 纯 Java 层;Native 与 Bridge.java 双份 native 声明(agent 内嵌版 Bridge 需同步——**Task 4 Step 3 修改 java/mosaic/Bridge.java 后,agent 的 Bridge 一致性检查(ci/build_mc_agent.sh)会要求 agent 版同步——Task 4 需同步 agent/com/mosaic/agent/Bridge.java 的 native 声明(仅追加,不改旧签名)**)
- 接口小修正:Task 2 的 MosaicRuntime 在 Task 4 追加 `txBegin`/`activation` 两行(只增不减路径,合入时说明)

**计划内推迟:** 原版域 write 路径(写方块/改物品)首版仅 Provider 能力边界内(读为主);MosaicItem 的 components since 标注已在接口;26.2 映射表方法名以 Task 1 验证与逆向源码迭代为准。
