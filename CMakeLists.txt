cmake_minimum_required(VERSION 3.20)
project(veyra VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(VEYRA_BUILD_BENCH "Build benchmark harness" ON)
option(VEYRA_WITH_TEXT "Build HarfBuzz text shaping support" OFF)
option(VEYRA_WITH_UNICODE "Build ICU-based Unicode editing, bidi, and line breaking" OFF)
option(VEYRA_WITH_FREETYPE "Build FreeType glyph rasterization support" OFF)
option(VEYRA_WITH_SDL3 "Build the SDL3 software window backend" OFF)
set(VEYRA_PARADIGM "all" CACHE STRING "arena, reactive, ecs, or all")
set_property(CACHE VEYRA_PARADIGM PROPERTY STRINGS arena reactive ecs all)
set(valid_paradigms arena reactive ecs all)

if(NOT VEYRA_PARADIGM IN_LIST valid_paradigms)
 message(FATAL_ERROR "Invalid VEYRA_PARADIGM: ${VEYRA_PARADIGM}")
endif()

add_library(veyra STATIC
 src/core/focus_navigation.cpp
 src/core/hit_test.cpp
 src/core/node_id_pool.cpp
 src/core/node_tree.cpp
 src/core/semantics.cpp
 src/layout/flex.cpp
 src/layout/grid.cpp
 src/layout/grid_placement.cpp
 src/layout/measure.cpp
 src/layout/stack.cpp
 src/layout/virtual_list.cpp
 src/text/glyph_mask_cache.cpp
 src/text/text_editor_buffer.cpp
 src/render/software_renderer.cpp
 src/engines.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra PUBLIC cxx_std_23)

enable_testing()

set(veyra_build_config "$<CONFIG>")
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
 set(veyra_build_config "unspecified")
endif()

if(VEYRA_WITH_TEXT)
 find_package(harfbuzz CONFIG REQUIRED)
 add_library(veyra_text STATIC
  src/text/glyph_run.cpp
  src/text/shaped_run_cache.cpp
  src/text/shaper.cpp
 )
 target_include_directories(veyra_text PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_text PUBLIC veyra harfbuzz::harfbuzz)
 target_compile_features(veyra_text PUBLIC cxx_std_23)

 add_executable(veyra_text_shaper_test tests/text_shaper_test.cpp)
 target_link_libraries(veyra_text_shaper_test PRIVATE veyra_text)
 add_test(NAME text_shaper_behavior COMMAND veyra_text_shaper_test)

 add_executable(veyra_shaped_run_cache_test tests/shaped_run_cache_test.cpp)
 target_link_libraries(veyra_shaped_run_cache_test PRIVATE veyra_text)
 add_test(NAME shaped_run_cache_behavior COMMAND veyra_shaped_run_cache_test)

 if(VEYRA_BUILD_BENCH)
  add_executable(veyra_text_shaper_bench bench/text_shaper.cpp)
  target_link_libraries(veyra_text_shaper_bench PRIVATE veyra_text)
  target_compile_definitions(veyra_text_shaper_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )

  add_executable(veyra_shaped_run_cache_bench bench/shaped_run_cache.cpp)
  target_link_libraries(veyra_shaped_run_cache_bench PRIVATE veyra_text)
  target_compile_definitions(veyra_shaped_run_cache_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(NAME shaped_run_cache_benchmark_smoke COMMAND veyra_shaped_run_cache_bench)
 endif()
endif()

if(VEYRA_WITH_UNICODE)
 find_package(ICU 74 REQUIRED COMPONENTS i18n uc)
 add_library(veyra_unicode STATIC
  src/text/bidi_resolver_icu.cpp
  src/text/grapheme_icu.cpp
  src/text/line_breaker_icu.cpp
  src/text/utf8.cpp
 )
 target_include_directories(veyra_unicode PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_unicode PUBLIC veyra ICU::i18n ICU::uc)
 target_compile_features(veyra_unicode PUBLIC cxx_std_23)

 add_executable(veyra_grapheme_navigation_test tests/grapheme_navigation_test.cpp)
 target_link_libraries(veyra_grapheme_navigation_test PRIVATE veyra_unicode)
 add_test(NAME grapheme_navigation_behavior COMMAND veyra_grapheme_navigation_test)

 add_executable(veyra_line_breaker_test tests/line_breaker_test.cpp)
 target_link_libraries(veyra_line_breaker_test PRIVATE veyra_unicode)
 add_test(NAME text_line_breaker_behavior COMMAND veyra_line_breaker_test)

 add_executable(veyra_bidi_resolver_test tests/bidi_resolver_test.cpp)
 target_link_libraries(veyra_bidi_resolver_test PRIVATE veyra_unicode)
 add_test(NAME text_bidi_resolver_behavior COMMAND veyra_bidi_resolver_test)

 if(VEYRA_WITH_TEXT)
  add_library(veyra_font_fallback STATIC src/text/font_fallback_icu.cpp)
  target_include_directories(
   veyra_font_fallback
   PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_link_libraries(veyra_font_fallback PUBLIC veyra_text veyra_unicode)
  target_compile_features(veyra_font_fallback PUBLIC cxx_std_23)

  add_executable(veyra_font_fallback_test tests/font_fallback_test.cpp)
  target_link_libraries(veyra_font_fallback_test PRIVATE veyra_font_fallback)
  add_test(NAME text_font_fallback_behavior COMMAND veyra_font_fallback_test)

  if(VEYRA_BUILD_BENCH)
   add_executable(veyra_font_fallback_bench bench/font_fallback.cpp)
   target_link_libraries(veyra_font_fallback_bench PRIVATE veyra_font_fallback)
   target_compile_definitions(veyra_font_fallback_bench PRIVATE
    "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
    "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
    "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
   )
   add_test(NAME text_font_fallback_benchmark_smoke COMMAND veyra_font_fallback_bench)
  endif()
 endif()

 if(VEYRA_BUILD_BENCH)
  add_executable(veyra_bidi_resolver_bench bench/bidi_resolver.cpp)
  target_link_libraries(veyra_bidi_resolver_bench PRIVATE veyra_unicode)
  target_compile_definitions(veyra_bidi_resolver_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(NAME text_bidi_resolver_benchmark_smoke COMMAND veyra_bidi_resolver_bench)

  add_executable(veyra_line_breaker_bench bench/line_breaker.cpp)
  target_link_libraries(veyra_line_breaker_bench PRIVATE veyra_unicode)
  target_compile_definitions(veyra_line_breaker_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(NAME text_line_breaker_benchmark_smoke COMMAND veyra_line_breaker_bench)
 endif()

 if(VEYRA_BUILD_BENCH)
  add_executable(veyra_grapheme_navigation_bench bench/grapheme_navigation.cpp)
  target_link_libraries(veyra_grapheme_navigation_bench PRIVATE veyra_unicode)
  target_compile_definitions(veyra_grapheme_navigation_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(NAME grapheme_navigation_benchmark_smoke COMMAND veyra_grapheme_navigation_bench)
 endif()
endif()

if(VEYRA_WITH_FREETYPE)
 find_package(Freetype 2.14.3 REQUIRED)
 add_library(veyra_freetype STATIC src/text/freetype_rasterizer.cpp)
 target_include_directories(
  veyra_freetype
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_freetype PUBLIC veyra Freetype::Freetype)
 target_compile_features(veyra_freetype PUBLIC cxx_std_23)

 add_executable(veyra_freetype_rasterizer_test tests/freetype_rasterizer_test.cpp)
 target_link_libraries(veyra_freetype_rasterizer_test PRIVATE veyra_freetype)
 add_test(NAME freetype_rasterizer_behavior COMMAND veyra_freetype_rasterizer_test)

 if(VEYRA_BUILD_BENCH)
  add_executable(veyra_freetype_color_bitmap_bench bench/freetype_color_bitmap.cpp)
  target_link_libraries(veyra_freetype_color_bitmap_bench PRIVATE veyra_freetype)
  target_compile_definitions(veyra_freetype_color_bitmap_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(
   NAME freetype_color_bitmap_benchmark_smoke
   COMMAND veyra_freetype_color_bitmap_bench
  )

  add_executable(veyra_freetype_rasterizer_bench bench/freetype_rasterizer.cpp)
  target_link_libraries(veyra_freetype_rasterizer_bench PRIVATE veyra_freetype)
  target_compile_definitions(veyra_freetype_rasterizer_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
 endif()
endif()

if(VEYRA_WITH_SDL3)
 find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3)
 add_library(veyra_sdl3 STATIC src/platform/sdl3_window.cpp)
 target_include_directories(
  veyra_sdl3
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_sdl3 PUBLIC veyra SDL3::SDL3)
 target_compile_features(veyra_sdl3 PUBLIC cxx_std_23)

 add_executable(veyra_sdl3_window_test tests/sdl3_window_test.cpp)
 target_link_libraries(veyra_sdl3_window_test PRIVATE veyra_sdl3)
 add_test(NAME sdl3_window_behavior COMMAND veyra_sdl3_window_test)
 set_tests_properties(sdl3_window_behavior PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

foreach(paradigm arena reactive ecs)
 if(VEYRA_PARADIGM STREQUAL "all" OR VEYRA_PARADIGM STREQUAL paradigm)
  string(TOUPPER "${paradigm}" upper)
  target_compile_definitions(veyra PRIVATE "VEYRA_${upper}=1")
 endif()
endforeach()

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

add_executable(veyra_hit_test_test tests/hit_test_test.cpp)
target_link_libraries(veyra_hit_test_test PRIVATE veyra)
add_test(NAME hit_test_behavior COMMAND veyra_hit_test_test)

add_executable(veyra_focus_navigation_test tests/focus_navigation_test.cpp)
target_link_libraries(veyra_focus_navigation_test PRIVATE veyra)
add_test(NAME focus_navigation_behavior COMMAND veyra_focus_navigation_test)

add_executable(veyra_semantics_test tests/semantics_test.cpp)
target_link_libraries(veyra_semantics_test PRIVATE veyra)
add_test(NAME semantics_behavior COMMAND veyra_semantics_test)

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

add_executable(veyra_software_renderer_snapshot_test tests/software_renderer_snapshot_test.cpp)
target_link_libraries(veyra_software_renderer_snapshot_test PRIVATE veyra)
add_test(NAME software_renderer_snapshot COMMAND veyra_software_renderer_snapshot_test)

add_executable(veyra_software_renderer_line_test tests/software_renderer_line_test.cpp)
target_link_libraries(veyra_software_renderer_line_test PRIVATE veyra)
add_test(NAME software_renderer_line_behavior COMMAND veyra_software_renderer_line_test)

add_executable(veyra_glyph_mask_cache_test tests/glyph_mask_cache_test.cpp)
target_link_libraries(veyra_glyph_mask_cache_test PRIVATE veyra)
add_test(NAME glyph_mask_cache_behavior COMMAND veyra_glyph_mask_cache_test)

add_executable(veyra_text_editor_buffer_test tests/text_editor_buffer_test.cpp)
target_link_libraries(veyra_text_editor_buffer_test PRIVATE veyra)
add_test(NAME text_editor_buffer_behavior COMMAND veyra_text_editor_buffer_test)

if(VEYRA_BUILD_BENCH)
 add_executable(veyra_bench bench/main.cpp)
 target_link_libraries(veyra_bench PRIVATE veyra)
 add_executable(veyra_flex_layout_bench bench/flex_layout.cpp)
 target_link_libraries(veyra_flex_layout_bench PRIVATE veyra)
 add_executable(veyra_grid_layout_bench bench/grid_layout.cpp)
 target_link_libraries(veyra_grid_layout_bench PRIVATE veyra)
 add_executable(veyra_virtual_list_bench bench/virtual_list.cpp)
 target_link_libraries(veyra_virtual_list_bench PRIVATE veyra)
 add_executable(veyra_focus_navigation_bench bench/focus_navigation.cpp)
 target_link_libraries(veyra_focus_navigation_bench PRIVATE veyra)
 add_executable(veyra_hit_test_bench bench/hit_test.cpp)
 target_link_libraries(veyra_hit_test_bench PRIVATE veyra)
 add_executable(veyra_software_renderer_bench bench/software_renderer.cpp)
 target_link_libraries(veyra_software_renderer_bench PRIVATE veyra)
 add_executable(veyra_glyph_mask_cache_bench bench/glyph_mask_cache.cpp)
 target_link_libraries(veyra_glyph_mask_cache_bench PRIVATE veyra)
 add_executable(veyra_text_editor_buffer_bench bench/text_editor_buffer.cpp)
 target_link_libraries(veyra_text_editor_buffer_bench PRIVATE veyra)
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
 target_compile_definitions(veyra_focus_navigation_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_hit_test_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_software_renderer_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_glyph_mask_cache_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_text_editor_buffer_bench PRIVATE
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
  NAME focus_navigation_benchmark_smoke
  COMMAND veyra_focus_navigation_bench --items 16 --queries 100 --warmup 10
 )
 add_test(
  NAME hit_test_benchmark_smoke
  COMMAND veyra_hit_test_bench --items 16 --queries 100 --warmup 10
 )
 add_test(
  NAME software_renderer_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2
 )
 add_test(
  NAME software_renderer_alpha_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --translucent
 )
 add_test(
  NAME software_renderer_mask_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --mask
 )
 add_test(
  NAME software_renderer_image_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --image
 )
 add_test(
  NAME software_renderer_border_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --border
 )
 add_test(
  NAME software_renderer_line_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --line
 )
 add_test(
  NAME glyph_mask_cache_benchmark_smoke
  COMMAND veyra_glyph_mask_cache_bench --glyphs 32 --queries 100 --warmup 10
 )
 add_test(
  NAME text_editor_buffer_benchmark_smoke
  COMMAND veyra_text_editor_buffer_bench --bytes 1024 --queries 100 --warmup 10
 )
endif()
