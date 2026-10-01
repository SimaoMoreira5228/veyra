cmake_minimum_required(VERSION 3.20)
project(veyra VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(VEYRA_BUILD_BENCH "Build benchmark harness" ON)
set(VEYRA_PARADIGM "all" CACHE STRING "arena, reactive, ecs, or all")
set_property(CACHE VEYRA_PARADIGM PROPERTY STRINGS arena reactive ecs all)
set(valid_paradigms arena reactive ecs all)

if(NOT VEYRA_PARADIGM IN_LIST valid_paradigms)
 message(FATAL_ERROR "Invalid VEYRA_PARADIGM: ${VEYRA_PARADIGM}")
endif()

add_library(veyra STATIC
 src/core/node_id_pool.cpp
 src/engines.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra PUBLIC cxx_std_20)

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

if(VEYRA_BUILD_BENCH)
 add_executable(veyra_bench bench/main.cpp)
 target_link_libraries(veyra_bench PRIVATE veyra)
 set(veyra_build_config "$<CONFIG>")
 if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(veyra_build_config "unspecified")
 endif()
 target_compile_definitions(veyra_bench PRIVATE
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
endif()
