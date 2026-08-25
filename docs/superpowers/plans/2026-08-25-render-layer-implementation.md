# 渲染层现代化 + 统一渲染 API 实施计划(存档)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **⚠️ 存档状态:本计划不实施。** 设计已确认(见 `docs/superpowers/specs/2026-08-25-render-layer-direction-design.md`),实施待"服务端阶段完成"后按需启用。启用时按本计划执行,未决项在对应任务中定稿。

**Goal:** 让老版本 Minecraft(第一靶子 1.8.9,远期最低兼容 1.0)运行在新 JVM(Java 22+)上获得新 Java 特性,并给 Mod 作者统一渲染 API 面(`mosaic.render.*` 稳定扩展面)。

**Architecture:** 方案 A 绑定替换 + 渐进接管。核心机制:**FFM 重实现 LWJGL2 API 同名类**(`org.lwjgl.opengl.GL11`/`Display`/`Keyboard`/`Mouse` 同 FQN 替换,原版代码 invokestatic 调用零修改)——窗口/输入/音频由 SDL3 承担(FFM 绑定),固定管线 GL 函数由 FFM 直接绑定系统 GL;原版渲染代码在 SDL3 的 GL 兼容上下文照跑(不翻译);渲染入口(LevelRenderer.renderLevel)可切换,Mod 渲染实现渐进接管。

**Tech Stack:** Java 22+ FFM(`java.lang.foreign`)、SDL3(C 库,FFM 纯 Java 绑定,无 JNI 编译)、自研 javaagent(复用 M4 机制)、1.8.9 客户端(MCP 映射 + javap 核实)、CMake/ctest/既有门禁不受影响。

## Global Constraints

- **绑定 = FFM 纯 Java**(JDK 22+;零 JNI 编译;`--enable-native-access=ALL-UNNAMED` 或等价压制);SDL3 原生库随 agent 分发 + dlopen。
- **原版代码零修改**:注入只换宿主(同名类替换),不翻译 GL 调用、不逐调用重定向;原版渲染代码原样执行。
- **混淆名核实纪律**:1.8.9 注入点(MCP 映射 + javap 实测客户端 jar);1.0 未混淆直读;远期版本沿用既有方法论。
- **jar 零修改**:注入 = javaagent + ClassFileTransformer(既有 M4 机制),不修改原版 jar。
- **只增不减**:`mosaic.render.*` 进稳定 API 面(@Since(1)/契约测试/兼容套件);API 设计文档补"渲染扩展域"条款。
- **E2E 诚实纪律**:起窗/帧率/输入证据实测;不可达路径如实标注。
- **验收基线**:1.8.9 客户端在 Java 22+ 启动,起窗口、原版渲染正常、输入/音频正常。
- 服务端阶段优先:本计划不实施,启用时按任务顺序执行。

---

### Task 1: 1.8.9 客户端注入面核实(前置侦察)

**Files:**
- Create: `docs/render/189-injection-survey.md`(核实报告,实施期的第一产物)

**Interfaces:**
- Consumes: 无。
- Produces: 注入点核实表(类名/方法签名/混淆状态)、LWJGL2 依赖清单、渲染栈事实(固定管线 GL 子集)。

- [ ] **Step 1: 确认 1.8.9 客户端 jar 与映射就位**
  - 来源:`~/minecraft1.8.9/`(mcp918 源码 + uber jar + mc_install/libraries——核实客户端 jar 具体路径与版本号;缺失时按既有 setup 脚本方式下载 1.8.9 client jar + 1.8.9-1.8.9-client.json 的 libraries 清单)。
  - 记录:jar 路径、MD5、MCP 映射文件(mcp918 的 conf 或等价)。
- [ ] **Step 2: 核实注入点(全部 javap 实测 + MCP 映射对照)**
  - 窗口初始化:`net.minecraft.client.Minecraft` 的 `run()`/启动路径中创建 Display 的调用点(1.8.9:Display.create 在 Minecraft 启动序列);
  - 渲染入口:1.8.9 `LevelRenderer.renderLevel`(MCP 名;1.8.9 未用 ProGuard 但官方 client jar 是混淆的——核实混淆名与 MCP 映射的对应,方法论同 1.20.1 server_mappings,来源 mcp918);
  - 输入:LWJGL2 `Keyboard`/`Mouse` 静态类的使用面(原版代码直接调用点清单,量化);
  - 音频:`org.lwjgl.openal` 使用面(OpenAL 绑定,LWJGL2 提供)。
  - 产出:注入点表(类名/签名/调用点密度),标注哪些用"同名类替换"覆盖(类替换后原版调用点自动指向新实现——应覆盖全部;核实无直接 JNI/static native 逃逸)。
- [ ] **Step 3: LWJGL2 API 子集清单**
  - 从 Step 2 的调用点清单归纳 `org.lwjgl.opengl.GL11/GL12/GL20…`、`Display`、`Keyboard`、`Mouse`、`openal` 被 1.8.9 实际使用的符号子集(函数名 + 签名);这是 FFM 重实现的目标 API 面。
  - 产出:符号清单(按类分组,标注签名)。
- [ ] **Step 4: 渲染栈事实记录**
  - 1.8.9 使用的 GL 特性面(固定管线 glBegin/glEnd 等 + GL11 拓展/兼容性调用);SDL3 GL 上下文兼容性要求(GL 2.1 兼容上下文或等价——SDL_GL_SetAttribute 配置);
  - 1.8.9 在新 JVM 上的已知障碍清单(渲染层之外:移除的 API/Unsafe/启动器——如实记录,标注哪些由 agent 处理、哪些需单独适配)。
- [ ] **Step 5: 提交核实报告**
  - `git add docs/render/189-injection-survey.md` + `git commit -m "docs: 1.8.9 client injection survey (render layer, archived)"`。

**Verification:** 报告完整(注入点表/符号清单/障碍清单),每项注入点有 javap 输出摘录;符号清单可直接作为 Task 2 的 FFM 绑定目标。

---

### Task 2: FFM 绑定层(窗口/输入/音频/GL 上下文/固定管线 GL)

**Files:**
- Create: `agent/render/` 下 FFM 绑定源(包名实施时定,建议 `mosaic.render.internal.sdl` / `mosaic.render.internal.gl`)
- Create: `agent/render/BindingSmokeTest.java`(无 MC 环境自测)
- Modify: `agent/` 构建脚本(agent jar 并入 SDL3 原生库 + 绑定类)

**Interfaces:**
- Consumes: Task 1 的符号清单。
- Produces:
  - SDL3 绑定子集:`SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)`、`SDL_CreateWindow`、`SDL_PollEvent`/`SDL_Event` 结构体布局、`SDL_GL_SetAttribute`/`SDL_GL_CreateContext`/`SDL_GL_SwapWindow`/`SDL_GL_DeleteContext`、输入(`SDL_PumpEvents`/`SDL_GetKeyboardState`/鼠标)、音频子集;
  - GL 绑定子集:固定管线函数(glBegin/glEnd/glVertex/glColor/glTexCoord/glMatrixMode/glLoadIdentity/glOrtho/glViewport/glClear/glClearColor/glBindTexture/glEnable/glDisable/glGenTextures/glDeleteTextures/glDrawArrays/glDrawElements/glShadeModel/glBlendFunc/glDepthFunc 等,按 Task 1 清单);
  - LWJGL2 同名类骨架:`org.lwjgl.opengl.GL11`(静态类,FFM 实现)、`org.lwjgl.opengl.Display`、`org.lwjgl.opengl.Keyboard`、`org.lwjgl.opengl.Mouse`(同 FQN,签名兼容 Task 1 清单)。

- [ ] **Step 1: FFM 绑定骨架(写绑定 + 冒烟测试)**
  - `BindingSmokeTest`:FFM `Linker.nativeLinker()` downcall 声明 `SDL_Init`/`SDL_CreateWindow`/`SDL_GL_CreateContext`/`SDL_GL_SwapWindow`/`SDL_DestroyWindow`/`SDL_Quit`,无 MC 环境创建窗口(需显示服务器;无头环境用 SDL_VIDEODRIVER=dummy 验证调用链不断);
  - TDD:先写测试(dummy 驱动下 SDL_Init→CreateWindow→GL_CreateContext→Swap→Quit 全链),再写绑定;
  - `--enable-native-access` 与 JVM 参数记录在测试脚本。
- [ ] **Step 2: SDL_Event 结构体布局(FFM MemoryLayout)**
  - `SDL_Event` 是 union(~56B):声明 `MemoryLayout`(type 字段 + 各事件结构体重叠布局),`SDL_PollEvent(MemorySegment)` 读事件类型(type)与字段;
  - 事件类型覆盖:SDL_QUIT/SDL_KEYDOWN/KEYUP/SDL_MOUSEMOTION/BUTTON/WHEEL/SDL_WINDOWEVENT。
- [ ] **Step 3: GL 绑定(固定管线子集)**
  - 从 Task 1 符号清单生成 FFM downcall 声明(GL 函数签名;注意 void*/纹理句柄用 MemorySegment;GLenum/GLint 用 int);
  - 绑定自测:`glClearColor`→`glClear(GL_COLOR_BUFFER_BIT)`→读像素(glReadPixels 或 FBO 读回)验证像素颜色(证据:读回值 == 设定值)。
- [ ] **Step 4: LWJGL2 同名类实现(静态 API 面)**
  - `org.lwjgl.opengl.GL11`:静态方法逐一映射到 FFM GL 绑定(签名与 LWJGL2 一致:int/float/ByteBuffer 参数——ByteBuffer 用 MemorySegment 转换);
  - `Display`(create/update/swapBuffers/isCloseRequested/destroy/setTitle/getWidth/getHeight)、`Keyboard`(isKeyDown/getEventKey)、`Mouse`(isButtonDown/getX/getY/getDX/getDY);
  - 冒烟测试:调用 GL11.glClear 等走 FFM 全链。
- [ ] **Step 5: SDL3 原生库分发**
  - 构建脚本:SDL3 预编译库(linux-x64 先行)解包进 agent jar,运行期解出 + `System.load`;记录下载源与版本;
  - 无 SDL3 库时绑定初始化失败 → 明确错误信息(不静默)。
- [ ] **Step 6: 提交**
  - `git commit -m "feat: FFM SDL3/GL binding layer (window/input/audio/fixed-pipeline GL)"`。

**Verification:** BindingSmokeTest 全链绿(无头 dummy 驱动 + 有显示环境各跑一次);LWJGL2 同名类与 Task 1 符号清单签名逐一对应;像素读回证据。

---

### Task 3: 1.8.9 客户端接管(窗口/输入/渲染宿主替换)

**Files:**
- Modify: `agent/com/mosaic/agent/MosaicTransformer.java`(类替换机制:同名类由 agent 提供、bootstrap 优先)
- Modify: `agent/com/mosaic/agent/MosaicAgent.java`(客户端模式启动参数区分)
- Create: `agent/render/189/WindowHook.java`(窗口接管钩子,注入 Display.create 等价点)
- Create: `ci/run_render_e2e_189.sh`(1.8.9 客户端 E2E)

**Interfaces:**
- Consumes: Task 2 的 LWJGL2 同名类 + SDL3/GL 绑定。
- Produces:
  - 1.8.9 客户端在 Java 22+ 启动:起窗口(SDL3 创建)、原版渲染在 SDL3 GL 兼容上下文照跑、输入/音频可用;
  - E2E 证据:启动日志、帧数采样(如 60s 内帧计数)、截图(像素证据)。

- [ ] **Step 1: 类替换机制**
  - agent 提供 `org.lwjgl.opengl.GL11` 等同名类:经 `Instrumentation.appendToBootstrapClassLoaderSearch`(既有机制,agent jar 已在 bootstrap 搜索路径)或 Transformer 重定向——核实 M4 的 appendToBootstrap 覆盖顺序(bootstrap 优先于 app 类加载器,原版代码解析到 agent 的 GL11);
  - 注意:1.8.9 的 LWJGL2 库在 app classpath——同名类双份时解析优先级必须核实(测试:原版代码 invokestatic GL11.glClear 实际调用到 FFM 实现)。
- [ ] **Step 2: 窗口接管钩子**
  - 注入点:1.8.9 Minecraft 启动路径中创建窗口处(Task 1 核实)——hook 改为调用 FFM `Display.create`(SDL3 创建窗口 + GL 上下文);
  - Display 兼容:分辨率/标题/全屏语义对齐 1.8.9 调用面(isCloseRequested 等)。
- [ ] **Step 3: 输入/音频映射**
  - Keyboard/Mouse:SDL3 事件 → LWJGL2 静态状态(按键状态表/光标位置/增量);原版代码读取路径零修改;
  - 音频:SDL_Audio 子集(或首版禁用并如实标注——原版声音调用降级静音,标注为已知限制)。
- [ ] **Step 4: 1.8.9 E2E(Java 22+)**
  - `ci/run_render_e2e_189.sh`:Java 22+ 启动 1.8.9 客户端(agent 注入)→ 起窗口 → 单人生成世界(或菜单界面)——**菜单界面即可验证渲染栈**(原版 GUI 渲染 = 固定管线 GL 全链);证据:启动日志无 GL 异常、帧数采样、截图(菜单界面像素);
  - 世界生成全链(区块渲染)作为第二阶段验证(时间预算内);不可达项如实标注。
- [ ] **Step 5: 提交**
  - `git commit -m "feat: 1.8.9 client takeover (SDL3 window/input, fixed-pipeline GL host swap, Java 22+ E2E)"`。

**Verification:** E2E 绿:1.8.9 在 Java 22+ 起窗、菜单渲染帧数 > 0、输入事件可用;证据文件(日志/截图/帧数)保留;类替换优先级有测试(原版调用点确走 FFM)。

---

### Task 4: 渲染入口切换 + `mosaic.render.*` 初版

**Files:**
- Create: `java-api/mosaic/render/`(`MosaicRenderApi`/`MosaicRenderContext`/`MosaicCanvas` 等初版接口,@Since(1))
- Modify: `agent/com/mosaic/agent/MosaicTransformer.java`(LevelRenderer.renderLevel 入口注入:条件跳转)
- Create: `agent/render/189/RenderSwitchHook.java`(入口开关 + Mod 渲染实现注册)
- Create: `agent/render/sample/UiRenderer.java`(最小示例:接管 UI 层)

**Interfaces:**
- Consumes: Task 3 的接管链(窗口/上下文/GL)。
- Produces:
  - 渲染入口可切换:LevelRenderer.renderLevel 入口条件跳转(全局开关;开关开 → Mod 渲染实现接管,原版 GL 代码跳过;关 → 原版照跑);
  - `mosaic.render.*` 初版接口(最小:画布/绘制命令子集/窗口信息)。

- [ ] **Step 1: 入口注入(条件跳转)**
  - LevelRenderer.renderLevel(1.8.9 MCP 名)入口插入条件跳转:开关(静态 volatile)开 → 跳 Mod 渲染实现(同签名);关 → 原版方法体照跑;
  - 开关语义与线程安全(渲染线程单线程前提,注释注明)。
- [ ] **Step 2: mosaic.render.* 初版接口**
  - `MosaicRenderContext`(窗口尺寸/缩放/帧时间)、`MosaicCanvas`(绘制命令子集:清屏/绘制四边形/纹理采样——映射到 GL 固定管线或 SDL_GPU,实施时定)、`MosaicRenderApi`(注册渲染器/开关);
  - @Since(1) + javadoc 语义契约;契约测试骨架(无窗口环境:接口语义断言 + FFM 绑定冒烟复用)。
- [ ] **Step 3: 最小示例渲染器(UiRenderer)**
  - 接管 UI 层:开关开 → 示例渲染器画一个纯色背景 + 文本(或不带文本的色块——字体渲染留后续,如实标注);
  - E2E:开关开 → 截图证据(纯色画面 != 原版菜单);开关关 → 原版画面恢复。
- [ ] **Step 4: API 设计文档补条款**
  - `docs/superpowers/specs/2026-08-22-mosaic-api-design.md` 补"渲染扩展域"条款(对"只暴露原版 API"决策的正式扩展;mosaic.render.* 为自创扩展域,非 vanilla 投影)。
- [ ] **Step 5: 提交**
  - `git commit -m "feat: render entry switch + mosaic.render.* v1 (sample UI takeover)"`。

**Verification:** E2E:开关开/关截图差异证据;契约测试骨架绿;API 文档条款落盘。

---

### Task 5: 统一 API 契约 + 验收量化

**Files:**
- Modify: `tests/jni/vanilla/VanillaContractTest.java` 或新 `tests/render/`(渲染契约,实施时定位置)
- Modify: `README.md`(渲染层小节)
- Create: `docs/render/acceptance.md`(验收标准定稿)

**Interfaces:**
- Consumes: Task 4 的 mosaic.render.* 接口。
- Produces: 渲染契约测试(与版本无关环境)、验收标准量化。

- [ ] **Step 1: 契约测试**(渲染 API 语义锁定,无窗口环境)
  - 接口语义断言(尺寸/帧时间/开关语义)+ FFM 绑定冒烟复用;只增不减机制(兼容套件同款);
  - 双代(1.8.9/26.2 环境)契约的可行性核实:渲染 API 与 MC 版本无关(纯 Mosaic 扩展面)——契约在无 MC 环境即可跑,双代非必须(记录裁决)。
- [ ] **Step 2: 验收量化定稿**
  - 1.8.9 菜单界面帧率基线(≥ 30fps 目标?实施时定,注明硬件);
  - 输入延迟/音频可用性验收;
  - 记录在 `docs/render/acceptance.md`。
- [ ] **Step 3: README**
  - 渲染层小节(机制/状态/已知限制:音频首版降级、字体渲染留后续)。
- [ ] **Step 4: 提交**
  - `git commit -m "feat: render contracts + acceptance baseline (archived batch)"`。

**Verification:** 契约测试绿;验收文档完整;README 一致。

---

### Task 6(远期,启用时评估): 最低兼容 1.0 下探

**Files:**
- Create: `docs/render/10-injection-survey.md`
- Modify: `agent/render/`(版本适配层若需要)

- [ ] **Step 1: 1.0 注入面核实**(net.minecraft.src 未混淆,类名直读)
  - 窗口创建/渲染入口(LevelRenderer 等价)/LWJGL 2.x 老版本绑定差异(1.0 用 LWJGL 2.4~2.8 时代 API——符号清单差异);
  - 1.0 在新 JVM 上的障碍清单(Java 6 时代代码:移除 API/泛型旧用法/Unsafe)。
- [ ] **Step 2: 版本适配**(绑定差异吸收;若 1.0 符号集是 1.8.9 子集则零适配,核实后定)
- [ ] **Step 3: 1.0 E2E**(起窗 + 菜单渲染证据)
- [ ] **Step 4: 提交核实报告 + 适配 + 证据**

**Verification:** 1.0 在 Java 22+ 起窗证据;障碍清单完整(渲染层之外项如实标注处理方式)。

---

## Self-Review 记录(存档版)

- **Spec 覆盖**:动机(全栈现代化+统一 API)→ T2/T3/T4;方案 A(绑定替换+渐进接管)→ T3(绑定替换)/T4(入口切换);靶子 1.8.9 → T1-T5;远期 1.0 → T6;API 定位(mosaic.render.* 稳定扩展面)→ T4/T5;FFM 绑定 → T2;SDL3 无 GL 后端事实 → T2 的 GL 独立绑定(T3 原版照跑承担 GL 回退);未决项(绑定子集清单/接口设计/障碍清单/验收量化)→ T1/T4/T5 定稿。
- **占位符扫描**:无 TBD;实施时定稿项均标注决策点。
- **类型一致性**:LWJGL2 同名类(org.lwjgl.opengl.GL11/Display/Keyboard/Mouse)在 T2 定义、T3 使用一致;mosaic.render.* 接口 T4 定义、T5 契约使用一致。
- **启用条件**:服务端阶段完成;实施顺序 T1→T6;每个任务独立可验收。
