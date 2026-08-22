cmake_minimum_required(VERSION 3.20)
project(mosaic C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_library(mosaic_core STATIC
  src/runtime.c
  src/pack_reader.c
  src/pack_builder.c
  src/descriptor.c
  src/index.c
  src/working_set.c
  src/lifecycle.c
  src/trigger.c
  src/ownership.c
  src/eviction.c
  src/deps.c
  src/genroute.c
  src/tx.c
  src/sched.c
  src/events.c
  src/world.c
)
target_include_directories(mosaic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# M2-3:sched.c 仅 libc + pthread(静态库的 PRIVATE 链接依赖会传导到最终链接行)
target_link_libraries(mosaic_core PRIVATE m dl Threads::Threads)
# M4-1:mosaic_jni 共享库链接需要 PIC 静态库(对可执行链接无行为影响)
set_target_properties(mosaic_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()

add_executable(test_pack tests/test_pack.c)
target_link_libraries(test_pack mosaic_core)
add_test(NAME test_pack COMMAND test_pack)

add_executable(test_builder tests/test_builder.c)
target_link_libraries(test_builder mosaic_core)
add_test(NAME test_builder COMMAND test_builder)

add_executable(test_runtime tests/test_runtime.c)
target_link_libraries(test_runtime mosaic_core)
# M-2:前缀对 tiebreak 用例需 dispatch 真实执行(≥1),沿用 test_trigger 的 fixture 传递模式
add_test(NAME test_runtime COMMAND test_runtime $<TARGET_FILE:test_mod>)

add_executable(test_index tests/test_index.c)
target_link_libraries(test_index mosaic_core)
target_include_directories(test_index PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_index COMMAND test_index)

add_executable(test_working_set tests/test_working_set.c)
target_link_libraries(test_working_set mosaic_core)
target_include_directories(test_working_set PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_working_set COMMAND test_working_set)

add_library(test_mod MODULE tests/test_mod.c)
target_include_directories(test_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# M2-2b 修复回归:多补丁 so 路由(test_mod_v2/v3 的 code_off 4 语义与 test_mod
# 不同——+7 / +13,补丁 so_path 指向它们时物化必须 dlopen 新 .so)
add_library(test_mod_v2 MODULE tests/test_mod_v2.c)
target_include_directories(test_mod_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(test_mod_v3 MODULE tests/test_mod_v3.c)
target_include_directories(test_mod_v3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(test_badmod MODULE tests/test_badmod.c)
target_include_directories(test_badmod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test_lifecycle tests/test_lifecycle.c)
target_link_libraries(test_lifecycle mosaic_core)
target_include_directories(test_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)  # 偏差 D-3:ws_find 断言
add_test(NAME test_lifecycle COMMAND test_lifecycle $<TARGET_FILE:test_mod> $<TARGET_FILE:test_badmod>)

add_executable(test_trigger tests/test_trigger.c)
target_link_libraries(test_trigger mosaic_core)
target_include_directories(test_trigger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)  # 重入回归测试访问 rt->map(内部结构)
add_test(NAME test_trigger COMMAND test_trigger $<TARGET_FILE:test_mod>)

# 偏差 D-9-1:简报 Step 5 的 CMake 块未含 src/ 私有头目录,但测试用 ws_find
# (mosaic_internal.h 声明),缺它无法编译;沿用既有模式(test_lifecycle 等)。
add_executable(test_ownership tests/test_ownership.c)
target_link_libraries(test_ownership mosaic_core)
target_include_directories(test_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_ownership COMMAND test_ownership $<TARGET_FILE:test_mod>)

# ---- bench:10M synth universe + S1-S4 gate runner (Task 10) ----
add_library(synth_mod MODULE bench/synth_abi.c)
target_include_directories(synth_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 偏差:CMake 默认把 MODULE 库放 build/ 根目录且带 lib 前缀、可执行放 build/ 根,
# 而 bench_runner 默认 so 路径为 build/bench/synth_mod.so、简报 Step 6 命令为
# build/bench/bench_runner(简报 Step 4/6);对齐输出目录与文件名使原样命令可用
set_target_properties(synth_mod PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench
  PREFIX "")

add_executable(bench_runner bench/bench_runner.c bench/synth_universe.c)
target_include_directories(bench_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/bench)
target_link_libraries(bench_runner mosaic_core m dl)
set_target_properties(bench_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench)

# M1.5-A:运行时多 pack 合并支持(open_many + 范围表合并索引 + 跨 pack 派发)
add_executable(test_shards tests/test_shards.c)
target_link_libraries(test_shards mosaic_core)
target_include_directories(test_shards PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_shards COMMAND test_shards $<TARGET_FILE:test_mod>)

# M2-1:依赖图遍历 + 闭包解析(纯记录读取,不 dlopen → 无需 test_mod)
add_executable(test_deps tests/test_deps.c)
target_link_libraries(test_deps mosaic_core)
add_test(NAME test_deps COMMAND test_deps)

# M2-2a:generation 路由表(内部结构,含 builder 设置器往返,无需 test_mod)
add_executable(test_genroute tests/test_genroute.c)
target_link_libraries(test_genroute mosaic_core)
target_include_directories(test_genroute PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_genroute COMMAND test_genroute)

# M2-2b:补丁 pack 事务 API + 状态迁移 + 混合版本共存 + demote 回滚
# 修复回归:二次 commit 迁移源(C-1)、commit 后 mods 缓存失效(I-1)、多补丁
# 最新 so 路由(I-2)——需要 test_mod_v2/v3 的 so 路径参数
add_executable(test_tx tests/test_tx.c)
target_link_libraries(test_tx mosaic_core)
add_test(NAME test_tx COMMAND test_tx $<TARGET_FILE:test_mod> $<TARGET_FILE:test_mod_v2> $<TARGET_FILE:test_mod_v3>)

# M2-3:DAG 任务调度器(线程池 + 依赖/优先级/亲和性/取消/checkpoint)
# 并行物化集成用例需要 test_mod.so fixture + mosaic_internal.h(ACTIVE 状态断言)
add_executable(test_sched tests/test_sched.c)
target_link_libraries(test_sched mosaic_core)
target_include_directories(test_sched PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_sched COMMAND test_sched $<TARGET_FILE:test_mod>)

# M2-4:item 描述符表(v3)+ 冷态查询(纯记录读取,零 dlopen → 无需 test_mod;
# 不物化验证用不存在 so 路径 + 物化 ABI 失败自证)
add_executable(test_descriptor tests/test_descriptor.c)
target_link_libraries(test_descriptor mosaic_core)
add_test(NAME test_descriptor COMMAND test_descriptor)

# M3-1:事件类型 API v1(目录 200+ 事件 + 载荷签名 + 频率档);大事件集 pack
# 集成用例(300 事件构建→查找→派发执行订阅者)需要 test_mod.so fixture
add_executable(test_events tests/test_events.c)
target_link_libraries(test_events mosaic_core)
add_test(NAME test_events COMMAND test_events $<TARGET_FILE:test_mod>)

# M3-2:合成世界模拟器(世界 API + 频率档生成器 + 确定性;动作→事件派发/
# 生成器/载荷探针集成用例需要 test_mod.so fixture + code_off 5 探针)
add_executable(test_world tests/test_world.c)
target_link_libraries(test_world mosaic_core)
add_test(NAME test_world COMMAND test_world $<TARGET_FILE:test_mod>)

# M3-3:世界场景基准(S-W1 生命周期门禁、S-W2 工作集 vs 函数总数门禁、
# S-W3 吞吐诊断);world pack 构建逻辑在 world_bench.c 内部,
# 复用 synth_universe.c 的 rng/计时/RSS 助手
add_executable(world_bench bench/world_bench.c bench/synth_universe.c)
target_include_directories(world_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/bench)
target_link_libraries(world_bench mosaic_core m dl)
set_target_properties(world_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench)

# M4-1:JNI 测试用小 pack 生成器(1 模块 3 函数、player_join 事件、2 触发器)
add_executable(gen_test_pack bench/gen_test_pack.c)
target_link_libraries(gen_test_pack mosaic_core)

# M4-2:1.20.1 服务端世界 pack 生成器(1 模块 13 函数、5 事件、13 触发器)
add_executable(gen_world_pack bench/gen_world_pack.c)
target_link_libraries(gen_world_pack mosaic_core)

# ---- M4-1:JVM Bridge(JNI 双向通道骨架,零 MC 依赖) ----
# JDK 自动探测:JAVA_HOME 未设时 FindJNI 走 /usr/lib/jvm/*(含 default 链接);
# 输出 build/lib/libmosaic_jni.so(System.loadLibrary("mosaic_jni") 查找点)
find_package(JNI REQUIRED)
add_library(mosaic_jni SHARED src/jni/bridge.c)
target_include_directories(mosaic_jni PRIVATE ${JNI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mosaic_jni mosaic_core ${JNI_LIBRARIES})
set_target_properties(mosaic_jni PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
