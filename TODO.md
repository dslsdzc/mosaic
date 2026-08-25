# Mosaic TODO

> 项目级待办清单(非阻塞项汇总)。完整评审台账见 `.superpowers/sdd/progress.md`(git-ignored)。

## 未开始方向(按推荐序)

- [ ] **1.20.1 Provider(Vanilla1201Provider)** — Java mod 经稳定 API 在服务端跑的全链路最后一块:
      26.2/1.8.9 是契约环境(反射读 jar),1.20.1 是真实服务端但只有 agent;补 Provider 后
      java-api 上服务端 classpath,Java mod(订阅事件/注册命令/听监听器)在 1.20.1 真实运行的
      E2E 闭环。方法论现成(server_mappings + javap)。
- [ ] **渲染层(存档,服务端阶段完成后启用)** — 设计 `docs/superpowers/specs/2026-08-25-render-layer-direction-design.md`、
      计划 `docs/superpowers/plans/2026-08-25-render-layer-implementation.md`(6 任务,存档不实施)。
      动机:老版本 MC(靶子 1.8.9,远期最低 1.0)跑新 JVM(Java 22+)+ 统一渲染 API(mosaic.render.*);
      机制:FFM 重实现 LWJGL2 同名类,SDL3 全栈替换,原版渲染照跑 + 渲染入口可切换。
- [ ] **网络域写路径** — MosaicPacketSink→内核分发接线 + sendPacket 包编码(包内容序列化 v1 恒 0 的后续项)。
- [ ] **1.8.9 成功注册路径的版本锚点复核**(已有 Vanilla189ExtraTest,随 1.8.9 环境变化复核)。

## 已知小项(可选,顺手清)

- [ ] hex() 格式契约的仓库内回归断言(现靠 E2E 门禁正则间接覆盖;全值域等价证明在 task-4 报告)。
- [ ] EventImpl 与 agent 的监听器广播双实现去重或交叉引用强化(现仅注释交叉引用)。
- [ ] 包目录 PACKET_NAMES 逐项与 packets.c 的 N2 门禁已固化;未来新增包类时走 `ci/gen_packet_map.sh`。

## 流程注意(实施时必读)

- **MC_VER 升级**:`ci/gen_packet_map.sh` 内嵌类总数守卫(54)必红属期望行为——须同时更新
  INNER_CLASS_TOTAL、VARIANT_OUTERS 白名单与 packets.c 目录追加块(脚本注释已预告)。
- **新增 JNI**:必须 java/mosaic/Bridge.java ↔ agent/mosaic/Bridge.java 双份逐字同步 + grep-diff 门禁;
  验证必须覆盖 `ci/build_mc_agent.sh`(2026-08-25 LC-3 的 Critical 根因是验证绕过唯一执法点)。
- **只增不减**:API/事件目录/载荷只加不改;包目录加条目走追加块(不重编号既有 id)。
- **事件目录计数**:205 已派生化(无硬编码字面量);加事件只插排序位,双端(N2)自动比对。
- **E2E 诚实纪律**:客户端依赖路径如实标注;evidence 只写实测。
