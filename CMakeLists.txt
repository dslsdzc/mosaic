cmake_minimum_required(VERSION 3.20)
project(mosaic C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(mosaic_core STATIC
  src/runtime.c
  src/pack_reader.c
  src/pack_builder.c
  src/index.c
  src/working_set.c
  src/lifecycle.c
  src/trigger.c
  src/ownership.c
  src/eviction.c
)
target_include_directories(mosaic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mosaic_core PRIVATE m dl)

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
