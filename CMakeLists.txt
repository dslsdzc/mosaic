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
add_test(NAME test_runtime COMMAND test_runtime)

# 后续任务启用(先在文件中注释掉,避免缺文件编译失败):
# add_executable(test_runtime tests/test_runtime.c)   ... 见 Task 3-9
# add_executable(test_mod MODULE tests/test_mod.c)    ... 见 Task 6
# add_executable(test_badmod MODULE tests/test_badmod.c) ... 见 Task 6
# add_executable(synth_mod MODULE bench/synth_abi.c)  ... 见 Task 10
# add_executable(bench_runner bench/bench_runner.c bench/synth_universe.c) ... 见 Task 10
