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
