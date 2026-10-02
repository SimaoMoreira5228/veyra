cmake_minimum_required(VERSION 3.20)
project(veyra VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(VEYRA_BUILD_BENCH "Build benchmark harness" ON)
set(VEYRA_PARADIGM "all" CACHE STRING "arena, reactive, ecs, or all")
set_property(CACHE VEYRA_PARADIGM PROPERTY STRINGS arena reactive ecs all)
set(valid_paradigms arena reactive ecs all)

if(NOT VEYRA_PARADIGM IN_LIST valid_paradigms)
 message(FATAL_ERROR "Invalid VEYRA_PARADIGM: ${VEYRA_PARADIGM}")
endif()

add_library(veyra STATIC
 src/core/node_id_pool.cpp
 src/core/node_tree.cpp
 src/layout/flex.cpp
 src/layout/grid.cpp
 src/layout/grid_placement.cpp
 src/layout/measure.cpp
 src/layout/stack.cpp
 src/layout/virtual_list.cpp
 src/render/software_renderer.cpp
 src/engines.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra PUBLIC cxx_std_23)

foreach(paradigm arena reactive ecs)
 if(VEYRA_PARADIGM STREQUAL "all" OR VEYRA_PARADIGM STREQUAL paradigm)
  string(TOUPPER "${paradigm}" upper)
  target_compile_definitions(veyra PRIVATE "VEYRA_${upper}=1")
 endif()
endforeach()

enable_testing()
add_executable(veyra_engine_test tests/engine_test.cpp)
target_link_libraries(veyra_engine_test PRIVATE veyra)
add_test(NAME engine_behavior COMMAND veyra_engine_test)

add_executable(veyra_node_id_pool_test tests/node_id_pool_test.cpp)
target_link_libraries(veyra_node_id_pool_test PRIVATE veyra)
add_test(NAME node_id_pool_behavior COMMAND veyra_node_id_pool_test)

add_executable(veyra_node_tree_test tests/node_tree_test.cpp)
target_link_libraries(veyra_node_tree_test PRIVATE veyra)
add_test(NAME node_tree_behavior COMMAND veyra_node_tree_test)

add_executable(veyra_node_tree_replay_test tests/node_tree_replay_test.cpp)
target_link_libraries(veyra_node_tree_replay_test PRIVATE veyra)
add_test(NAME node_tree_replay COMMAND veyra_node_tree_replay_test)

add_executable(veyra_event_dispatch_test tests/event_dispatch_test.cpp)
target_link_libraries(veyra_event_dispatch_test PRIVATE veyra)
add_test(NAME event_dispatch_behavior COMMAND veyra_event_dispatch_test)

add_executable(veyra_flex_layout_test tests/flex_layout_test.cpp)
target_link_libraries(veyra_flex_layout_test PRIVATE veyra)
add_test(NAME flex_layout_behavior COMMAND veyra_flex_layout_test)

add_executable(veyra_grid_layout_test tests/grid_layout_test.cpp)
target_link_libraries(veyra_grid_layout_test PRIVATE veyra)
add_test(NAME grid_layout_behavior COMMAND veyra_grid_layout_test)

add_executable(veyra_stack_layout_test tests/stack_layout_test.cpp)
target_link_libraries(veyra_stack_layout_test PRIVATE veyra)
add_test(NAME stack_layout_behavior COMMAND veyra_stack_layout_test)

add_executable(veyra_virtual_list_test tests/virtual_list_test.cpp)
target_link_libraries(veyra_virtual_list_test PRIVATE veyra)
add_test(NAME virtual_list_behavior COMMAND veyra_virtual_list_test)

add_executable(veyra_layout_measure_test tests/layout_measure_test.cpp)
target_link_libraries(veyra_layout_measure_test PRIVATE veyra)
add_test(NAME layout_measure_behavior COMMAND veyra_layout_measure_test)

add_executable(veyra_software_renderer_test tests/software_renderer_test.cpp)
target_link_libraries(veyra_software_renderer_test PRIVATE veyra)
add_test(NAME software_renderer_behavior COMMAND veyra_software_renderer_test)

if(VEYRA_BUILD_BENCH)
 add_executable(veyra_bench bench/main.cpp)
 target_link_libraries(veyra_bench PRIVATE veyra)
 add_executable(veyra_flex_layout_bench bench/flex_layout.cpp)
 target_link_libraries(veyra_flex_layout_bench PRIVATE veyra)
 add_executable(veyra_grid_layout_bench bench/grid_layout.cpp)
 target_link_libraries(veyra_grid_layout_bench PRIVATE veyra)
 add_executable(veyra_virtual_list_bench bench/virtual_list.cpp)
 target_link_libraries(veyra_virtual_list_bench PRIVATE veyra)
 add_executable(veyra_software_renderer_bench bench/software_renderer.cpp)
 target_link_libraries(veyra_software_renderer_bench PRIVATE veyra)
 set(veyra_build_config "$<CONFIG>")
 if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(veyra_build_config "unspecified")
 endif()
 target_compile_definitions(veyra_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_flex_layout_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_grid_layout_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_virtual_list_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_software_renderer_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 add_test(
  NAME benchmark_smoke
  COMMAND veyra_bench --items 1000 --frames 10 --warmup 2 --updates 10 --seed 99
 )
 add_test(
  NAME benchmark_rejects_frame_overflow
  COMMAND veyra_bench --items 1 --frames 4294967295 --warmup 1 --updates 0
 )
 set_tests_properties(benchmark_rejects_frame_overflow PROPERTIES WILL_FAIL TRUE)
 add_test(
  NAME flex_layout_benchmark_smoke
  COMMAND veyra_flex_layout_bench --items 16 --frames 100 --warmup 10
 )
 add_test(
  NAME flex_layout_wrap_benchmark_smoke
  COMMAND veyra_flex_layout_bench --items 16 --frames 100 --warmup 10 --wrap
 )
 add_test(
  NAME flex_layout_baseline_benchmark_smoke
  COMMAND veyra_flex_layout_bench --items 16 --frames 100 --warmup 10 --baseline --wrap
 )
 add_test(
  NAME grid_layout_benchmark_smoke
  COMMAND veyra_grid_layout_bench --items 64 --frames 100 --warmup 10
 )
 add_test(
  NAME grid_fractional_benchmark_smoke
  COMMAND veyra_grid_layout_bench --items 64 --frames 100 --warmup 10 --fractional
 )
 add_test(
  NAME virtual_list_benchmark_smoke
  COMMAND veyra_virtual_list_bench --items 128 --queries 100 --warmup 10
 )
 add_test(
  NAME software_renderer_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2
 )
 add_test(
  NAME software_renderer_alpha_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --translucent
 )
endif()
