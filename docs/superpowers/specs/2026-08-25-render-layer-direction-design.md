# Mosaic 渲染层方向设计(存档)

> 日期:2026-08-25
> 状态:已确认(经头脑风暴逐节评审);**存档不实施**——服务端阶段完成后按需启用
> 用户明确:先把服务端搞好,本方向只写设计文档 + 实施计划

## 1. 定位

Mosaic 的未来方向之一:**渲染层现代化 + 统一渲染 API**。让老版本 Minecraft(第一靶子 1.8.9,远期最低兼容 1.0)运行在新 JVM(Java 22+)上获得新 Java 特性;同时给 Mod 作者一个统一渲染 API 面(`mosaic.render.*`)。

与既有决策的关系:
- **服务端优先不变**(M1 决策):渲染层是客户端侧扩展,不改变服务端路线;
- **"只暴露原版 API"决策正式扩展**(M5 方向):渲染 API 是 Mosaic 自创扩展域(原版没有统一渲染 API,无法投影 vanilla),进稳定 API 面;
- 与 M4 注入机制同族:客户端 agent 复用服务端 agent 的注入机制(ClassFileTransformer + 自研 hook 点)。

## 2. 动机链(头脑风暴确认)

1. 目标:任何老版本 MC 运行 Mosaic 的 jar 即可用上最新 Java 特性(虚拟线程/新 GC/FFM 等);
2. 障碍:老版本(1.8.9 为 LWJGL 2 + 固定管线 GL)的旧渲染栈是新 JVM/新系统上最难兼容的部分(老原生库、老窗口/输入代码);
3. 收敛:不逐调用翻译原版 GL(排除转换层方案——定制化过高),替换渲染栈宿主 + 渲染入口可切换;
4. 附带价值:Mod 作者获得统一渲染 API 面。

## 3. 已确认决策

| 决策点 | 结论 |
|---|---|
| 动机 | 全栈现代化 + 统一 API(兼容 + 新 API 面) |
| 替换粒度 | **方案 A:绑定替换 + 渐进接管**(否决全量替换——数人年工程;否决绑定替换为止——无统一 API) |
| 第一靶子 | **1.8.9**(LWJGL2 + 固定管线,新 JVM 最难兼容) |
| 远期最低 | **MC 1.0**(net.minecraft.src 未混淆 + LWJGL 2.x 老绑定) |
| API 定位 | **稳定扩展面** `mosaic.render.*`(只增不减/@Since/契约测试;非 vanilla 投影) |
| 绑定层 | **FFM 纯 Java 绑定**(JDK 22 正式化;零 JNI 编译;downcall 开销 ≈ JNI) |
| 目标 JVM | **22+**(21 只有 FFM preview) |

## 4. 技术事实(已核实)

- **SDL3 GPU API 后端仅 Vulkan / Direct3D 12 / Metal**——无 OpenGL 后端(官方 `SDL_CreateGPUDevice` 驱动名仅 "vulkan"/"direct3d12"/"metal";`SDL_GPUShaderFormat` 无 GLSL 标志;SDL 论坛确认 GPU API 是 modern-API wrapper)。统一 GPU API 面覆盖现代硬件;GL 回退(2009 年老卡)独立设计——由"原版渲染照跑"路径承担(SDL3 整体库仍支持 OpenGL:SDL_GL_CreateContext,只是不在 GPU API 内)。
- **SDL3 无官方 Java 绑定**:FFM 解决绑定层(纯 Java 声明签名);SDL3 原生库本体仍需随 agent 分发 + dlopen。
- **1.8.9 客户端混淆**:MCP 映射 + javap 核实方法论现成(~/minecraft1.8.9/mcp918);1.0 客户端未混淆(net.minecraft.src 直读)。

## 5. 架构(方案 A:绑定替换 + 渐进接管)

```
MC 客户端(1.8.9 → 最低 1.0;注入点按版本核实:窗口初始化 / LevelRenderer.renderLevel)
    ↕ 客户端 agent(M4 机制复用:ClassFileTransformer + 自研 hook 点)
SDL3 全栈(FFM 纯 Java 绑定:窗口/输入/音频/GL 上下文/GPU 设备)
    ├── 原版渲染代码:默认在 SDL3 的 GL 兼容上下文上照跑(不翻译,只换宿主)
    └── 统一渲染 API(mosaic.render.* 稳定扩展面,SDL_GPU 之上):
          渲染入口可切换 → Mod 渲染实现渐进接管(UI → 实体 → 世界)
```

关键性质:
- **兼容目标**(老版本跑新 JVM)靠"绑定替换"达成:窗口/输入/音频/GL 上下文现代化,原版渲染逻辑零改动在 SDL3 GL 上下文照跑——每版本可独立 E2E 验证(起窗口 + 原版渲染帧率证据);
- **替换目标**靠"入口切换"渐进达成:LevelRenderer.renderLevel 入口可切换,Mod 渲染实现接管后原版 GL 代码跳过;先 UI 层/最小渲染器验证管线;
- 两套渲染并存期(原版 GL 上下文 + SDL_GPU)有资源/上下文管理复杂度——接受,渐进迁移。

## 6. API 面:`mosaic.render.*` 稳定扩展面

- 新扩展域(非 vanilla 投影),与 runtime/vanilla 域并列;命名 `mosaic.render.*`;
- 进入稳定 API 面:只增不减、@Since 标注、契约测试、兼容套件——与既有 API 面同机制;
- 统一 API 在 SDL_GPU 之上:Mosaic 定义渲染 API(Mod 作者只接触统一 API,不关心 Vulkan/Metal/D3D12 差异);
- 具体接口设计(窗口/输入/音频子域是否进 API、绘制 API 形态)为暂存项——实施时定稿;
- **需修改** `docs/superpowers/specs/2026-08-22-mosaic-api-design.md`:补一条"渲染扩展域"条款(对"只暴露原版 API"决策的正式扩展)。

## 7. 注入面(客户端 hook)

- 复用 M4 机制:javaagent + ClassFileTransformer + 自研 hook 点,jar 零修改;
- hook 点按版本核实:1.8.9(MCP 映射 + javap)、1.0(未混淆)、1.20.1(ProGuard,server_mappings 方法论现成)、26.2(mojmap);
- 渲染入口(LevelRenderer.renderLevel 及各版本等价物)是核心注入点;窗口初始化注入点用于接管窗口创建。

## 8. 验收标准(暂存,实施时定稿)

- 1.8.9 客户端在 Java 22+ 上启动:起窗口、原版渲染正常(帧率证据)、输入/音频正常;
- 入口切换:最小示例渲染器接管(UI 层)E2E 证据;
- 统一 API 契约测试(双代:1.8.9 / 26.2 环境,同套件)。

## 9. 未决项(实施时定稿,不阻塞存档)

- SDL3 绑定子集清单(窗口/事件/输入/音频/GL 上下文/GPU 设备所需函数集);
- mosaic.render.* 具体接口设计;
- 老版本跑新 JVM 的渲染层外障碍清单(移除的 API、Unsafe、启动器——按版本核实);
- 验收标准的精确量化。

## 10. 关系与次序

- 服务端阶段优先(观测面补齐等),渲染层不实施;
- 实施启用时:本设计 → 实施计划(已附) → SDD 执行,第一靶子 1.8.9,验证机制后向 1.0 下探。
