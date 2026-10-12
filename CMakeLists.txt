cmake_minimum_required(VERSION 3.20)
project(veyra VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(VEYRA_BUILD_BENCH "Build benchmark harness" ON)
option(VEYRA_WITH_FILESYSTEM "Build asynchronous filesystem enumeration support" OFF)
option(VEYRA_WITH_TEXT "Build HarfBuzz text shaping support" OFF)
option(VEYRA_WITH_UNICODE "Build ICU-based Unicode editing, bidi, and line breaking" OFF)
option(VEYRA_WITH_FREETYPE "Build FreeType glyph rasterization support" OFF)
option(VEYRA_WITH_SDL3 "Build the SDL3 software window backend" OFF)
option(VEYRA_WITH_GLFW "Build the GLFW platform window backend" OFF)
set(VEYRA_PARADIGM "all" CACHE STRING "arena, reactive, ecs, or all")
set_property(CACHE VEYRA_PARADIGM PROPERTY STRINGS arena reactive ecs all)
set(valid_paradigms arena reactive ecs all)

if(NOT VEYRA_PARADIGM IN_LIST valid_paradigms)
 message(FATAL_ERROR "Invalid VEYRA_PARADIGM: ${VEYRA_PARADIGM}")
endif()

add_library(veyra STATIC
 src/chat/message_change_queue.cpp
 src/chat/row_height_cache.cpp
 src/files/file_list.cpp
 src/inspector/trace_recorder.cpp
 src/media/animation_frame_cache.cpp
 src/media/animation_timeline.cpp
 src/core/focus_navigation.cpp
 src/core/frame_scheduler.cpp
 src/core/hit_test.cpp
 src/core/node_id_pool.cpp
 src/core/node_tree.cpp
 src/core/semantics.cpp
 src/core/shortcut_map.cpp
 src/layout/breakpoints.cpp
 src/layout/container_queries.cpp
 src/layout/flex.cpp
 src/layout/grid.cpp
 src/layout/grid_placement.cpp
 src/layout/image_fit.cpp
 src/layout/image.cpp
 src/layout/measure.cpp
 src/layout/scroll_axis.cpp
 src/layout/stack.cpp
 src/layout/viewport.cpp
 src/layout/virtual_list.cpp
 src/render/damage_tracker.cpp
 src/render/display_batches.cpp
 src/render/scroll_shift.cpp
 src/render/software_command_validation.cpp
 src/render/software_path.cpp
 src/render/software_polyline.cpp
 src/render/software_rounded_rect.cpp
 src/render/software_image.cpp
 src/text/glyph_mask_cache.cpp
 src/text/glyph_image_cache.cpp
 src/text/text_edit_history.cpp
 src/text/text_editor_composition.cpp
 src/text/text_editor_buffer.cpp
 src/render/software_renderer.cpp
 src/platform/null_window.cpp
 src/platform/fake_services.cpp
 src/platform/window_router.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra PUBLIC cxx_std_23)

add_library(veyra_behaviors STATIC src/behaviors/pressable.cpp src/behaviors/toggle.cpp src/behaviors/selection.cpp src/behaviors/selection_typeahead.cpp src/behaviors/scroll.cpp src/behaviors/breadcrumb.cpp src/behaviors/tooltip.cpp src/behaviors/editable_text.cpp src/behaviors/overlay.cpp src/behaviors/toast.cpp src/behaviors/disclosure.cpp src/behaviors/drag_drop.cpp src/behaviors/datagrid.cpp src/behaviors/range.cpp src/behaviors/tabs.cpp src/behaviors/menu.cpp src/behaviors/menubar.cpp src/behaviors/combobox.cpp)
target_include_directories(veyra_behaviors PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(veyra_behaviors PUBLIC veyra)
target_compile_features(veyra_behaviors PUBLIC cxx_std_23)

add_library(veyra_style STATIC src/style/theme.cpp)
target_include_directories(veyra_style PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(veyra_style PUBLIC cxx_std_23)

add_library(veyra_widgets_basic STATIC src/widgets/basic/button.cpp src/widgets/basic/toggle.cpp src/widgets/basic/textfield.cpp)
target_include_directories(veyra_widgets_basic PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(veyra_widgets_basic PUBLIC veyra_behaviors veyra_style)
target_compile_features(veyra_widgets_basic PUBLIC cxx_std_23)

enable_testing()

set(veyra_build_config "$<CONFIG>")
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
 set(veyra_build_config "unspecified")
endif()

if(VEYRA_WITH_FILESYSTEM)
 find_package(Threads REQUIRED)
 add_library(veyra_filesystem STATIC
  src/files/directory_enumerator.cpp
  src/files/directory_history.cpp
  src/files/directory_watcher.cpp
  src/files/file_shell.cpp
  src/files/file_metadata.cpp
  src/files/file_thumbnail_queue.cpp
 )
 target_include_directories(veyra_filesystem PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_filesystem PUBLIC veyra Threads::Threads)
 if(WIN32)
  target_link_libraries(veyra_filesystem PUBLIC ole32 shell32)
 endif()
 target_compile_features(veyra_filesystem PUBLIC cxx_std_23)

 add_executable(veyra_directory_enumerator_test tests/directory_enumerator_test.cpp)
 target_link_libraries(veyra_directory_enumerator_test PRIVATE veyra_filesystem)
 add_test(NAME directory_enumerator_behavior COMMAND veyra_directory_enumerator_test)

 add_executable(veyra_directory_history_test tests/directory_history_test.cpp)
 target_link_libraries(veyra_directory_history_test PRIVATE veyra_filesystem)
 add_test(NAME directory_history_behavior COMMAND veyra_directory_history_test)

 if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(veyra_directory_watcher_test tests/directory_watcher_test.cpp)
  target_link_libraries(veyra_directory_watcher_test PRIVATE veyra_filesystem)
  add_test(NAME directory_watcher_behavior COMMAND veyra_directory_watcher_test)

  add_executable(veyra_file_shell_test tests/file_shell_test.cpp)
  target_link_libraries(veyra_file_shell_test PRIVATE veyra_filesystem)
  add_test(NAME file_shell_behavior COMMAND veyra_file_shell_test)
 endif()

 add_executable(veyra_file_thumbnail_queue_test tests/file_thumbnail_queue_test.cpp)
 target_link_libraries(veyra_file_thumbnail_queue_test PRIVATE veyra_filesystem)
 add_test(NAME file_thumbnail_queue_behavior COMMAND veyra_file_thumbnail_queue_test)

 add_executable(veyra_file_list_refresh_test tests/file_list_refresh_test.cpp)
 target_link_libraries(veyra_file_list_refresh_test PRIVATE veyra_filesystem)
 add_test(NAME file_list_refresh_behavior COMMAND veyra_file_list_refresh_test)

 if(VEYRA_BUILD_BENCH)
  add_executable(veyra_directory_enumerator_bench bench/directory_enumerator.cpp)
  target_link_libraries(veyra_directory_enumerator_bench PRIVATE veyra_filesystem)
  target_compile_definitions(veyra_directory_enumerator_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
  )
  add_test(
   NAME directory_enumerator_benchmark_smoke
   COMMAND veyra_directory_enumerator_bench --entries 64 --warmup 0 --queries 1
  )
 endif()
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

 add_executable(veyra_line_measure_cache_test tests/line_measure_cache_test.cpp)
 target_link_libraries(veyra_line_measure_cache_test PRIVATE veyra_text)
 add_test(NAME line_measure_cache_behavior COMMAND veyra_line_measure_cache_test)

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

 target_sources(veyra_sdl3 PRIVATE src/platform/sdl3_translate.cpp)

 add_executable(veyra_sdl3_translate_test tests/sdl3_translate_test.cpp)
 target_link_libraries(veyra_sdl3_translate_test PRIVATE veyra_sdl3)
 add_test(NAME sdl3_translate_behavior COMMAND veyra_sdl3_translate_test)
 set_tests_properties(sdl3_translate_behavior PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

if(VEYRA_WITH_GLFW)
 find_package(glfw3 3.4 CONFIG REQUIRED)
 if(TARGET glfw)
  set(veyra_glfw_target glfw)
 elseif(TARGET glfw3::glfw)
  set(veyra_glfw_target glfw3::glfw)
 else()
  message(FATAL_ERROR "glfw3 was found but did not define a supported CMake target")
 endif()
 add_library(veyra_glfw STATIC src/platform/glfw_window.cpp)
 target_include_directories(veyra_glfw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_glfw PUBLIC veyra PRIVATE ${veyra_glfw_target})
 target_compile_features(veyra_glfw PUBLIC cxx_std_23)

 add_executable(veyra_glfw_window_test tests/glfw_window_test.cpp)
 target_link_libraries(veyra_glfw_window_test PRIVATE veyra_glfw ${veyra_glfw_target})
 add_test(NAME glfw_window_behavior COMMAND veyra_glfw_window_test)
 set_tests_properties(glfw_window_behavior PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(VEYRA_PARADIGM STREQUAL "all")
 target_sources(veyra PRIVATE
  src/paradigms/arena.cpp
  src/paradigms/reactive.cpp
  src/paradigms/ecs.cpp
  src/paradigms/registry.cpp
 )
 target_compile_definitions(veyra PUBLIC VEYRA_ARENA=1 VEYRA_REACTIVE=1 VEYRA_ECS=1)
elseif(VEYRA_PARADIGM STREQUAL "arena")
 target_sources(veyra PRIVATE src/paradigms/arena.cpp)
 target_compile_definitions(veyra PUBLIC VEYRA_ARENA=1)
elseif(VEYRA_PARADIGM STREQUAL "reactive")
 target_sources(veyra PRIVATE src/paradigms/reactive.cpp)
 target_compile_definitions(veyra PUBLIC VEYRA_REACTIVE=1)
elseif(VEYRA_PARADIGM STREQUAL "ecs")
 target_sources(veyra PRIVATE src/paradigms/ecs.cpp)
 target_compile_definitions(veyra PUBLIC VEYRA_ECS=1)
endif()

add_executable(veyra_engine_test tests/engine_test.cpp)
target_link_libraries(veyra_engine_test PRIVATE veyra)
add_test(NAME engine_behavior COMMAND veyra_engine_test)

add_executable(veyra_file_list_test tests/file_list_test.cpp)
target_link_libraries(veyra_file_list_test PRIVATE veyra)
add_test(NAME file_list_behavior COMMAND veyra_file_list_test)

add_executable(veyra_file_thumbnail_cache_test tests/file_thumbnail_cache_test.cpp)
target_link_libraries(veyra_file_thumbnail_cache_test PRIVATE veyra)
add_test(NAME file_thumbnail_cache_behavior COMMAND veyra_file_thumbnail_cache_test)

add_executable(veyra_chat_row_height_cache_test tests/chat_row_height_cache_test.cpp)
target_link_libraries(veyra_chat_row_height_cache_test PRIVATE veyra)
add_test(NAME chat_row_height_cache_behavior COMMAND veyra_chat_row_height_cache_test)

add_executable(veyra_chat_list_burst_test tests/chat_list_burst_test.cpp)
target_link_libraries(veyra_chat_list_burst_test PRIVATE veyra)
add_test(NAME chat_list_burst_behavior COMMAND veyra_chat_list_burst_test)

add_executable(veyra_pressable_test tests/pressable_test.cpp)
target_link_libraries(veyra_pressable_test PRIVATE veyra_behaviors)
add_test(NAME pressable_behavior COMMAND veyra_pressable_test)

add_executable(veyra_theme_test tests/theme_test.cpp)
target_link_libraries(veyra_theme_test PRIVATE veyra_style)
add_test(NAME theme_tokens_behavior COMMAND veyra_theme_test)

add_executable(veyra_button_appearance_test tests/button_appearance_test.cpp)
target_link_libraries(veyra_button_appearance_test PRIVATE veyra_widgets_basic)
add_test(NAME button_appearance_behavior COMMAND veyra_button_appearance_test)

add_executable(veyra_toggle_test tests/toggle_test.cpp)
target_link_libraries(veyra_toggle_test PRIVATE veyra_behaviors)
add_test(NAME toggle_behavior COMMAND veyra_toggle_test)

add_executable(veyra_range_test tests/range_test.cpp)
target_link_libraries(veyra_range_test PRIVATE veyra_behaviors)
add_test(NAME range_behavior COMMAND veyra_range_test)

add_executable(veyra_spinbox_test tests/spinbox_test.cpp)
target_link_libraries(veyra_spinbox_test PRIVATE veyra_behaviors)
add_test(NAME spinbox_behavior COMMAND veyra_spinbox_test)

add_executable(veyra_selection_test tests/selection_test.cpp)
target_link_libraries(veyra_selection_test PRIVATE veyra_behaviors)
add_test(NAME selection_behavior COMMAND veyra_selection_test)

add_executable(veyra_tabs_test tests/tabs_test.cpp)
target_link_libraries(veyra_tabs_test PRIVATE veyra_behaviors)
add_test(NAME tabs_behavior COMMAND veyra_tabs_test)

add_executable(veyra_menu_test tests/menu_test.cpp)
target_link_libraries(veyra_menu_test PRIVATE veyra_behaviors)
add_test(NAME menu_behavior COMMAND veyra_menu_test)

add_executable(veyra_menubar_test tests/menubar_test.cpp)
target_link_libraries(veyra_menubar_test PRIVATE veyra_behaviors)
add_test(NAME menubar_behavior COMMAND veyra_menubar_test)

add_executable(veyra_combobox_test tests/combobox_test.cpp)
target_link_libraries(veyra_combobox_test PRIVATE veyra_behaviors)
add_test(NAME combobox_behavior COMMAND veyra_combobox_test)

add_executable(veyra_breadcrumb_test tests/breadcrumb_test.cpp)
target_link_libraries(veyra_breadcrumb_test PRIVATE veyra_behaviors)
add_test(NAME breadcrumb_behavior COMMAND veyra_breadcrumb_test)

add_executable(veyra_tooltip_test tests/tooltip_test.cpp)
target_link_libraries(veyra_tooltip_test PRIVATE veyra_behaviors)
add_test(NAME tooltip_behavior COMMAND veyra_tooltip_test)

add_executable(veyra_menu_placement_test tests/menu_placement_test.cpp)
target_link_libraries(veyra_menu_placement_test PRIVATE veyra)
add_test(NAME menu_placement_behavior COMMAND veyra_menu_placement_test)

add_executable(veyra_disclosure_test tests/disclosure_test.cpp)
target_link_libraries(veyra_disclosure_test PRIVATE veyra_behaviors)
add_test(NAME disclosure_behavior COMMAND veyra_disclosure_test)

add_executable(veyra_tree_cursor_test tests/tree_cursor_test.cpp)
target_link_libraries(veyra_tree_cursor_test PRIVATE veyra_behaviors)
add_test(NAME tree_cursor_behavior COMMAND veyra_tree_cursor_test)

add_executable(veyra_datagrid_test tests/datagrid_test.cpp)
target_link_libraries(veyra_datagrid_test PRIVATE veyra_behaviors)
add_test(NAME datagrid_behavior COMMAND veyra_datagrid_test)

add_executable(veyra_overlay_test tests/overlay_test.cpp)
target_link_libraries(veyra_overlay_test PRIVATE veyra_behaviors)
add_test(NAME overlay_behavior COMMAND veyra_overlay_test)

add_executable(veyra_toast_test tests/toast_test.cpp)
target_link_libraries(veyra_toast_test PRIVATE veyra_behaviors)
add_test(NAME toast_behavior COMMAND veyra_toast_test)

add_executable(veyra_drag_drop_test tests/drag_drop_test.cpp)
target_link_libraries(veyra_drag_drop_test PRIVATE veyra_behaviors)
add_test(NAME drag_drop_behavior COMMAND veyra_drag_drop_test)

add_executable(veyra_toggle_appearance_test tests/toggle_appearance_test.cpp)
target_link_libraries(veyra_toggle_appearance_test PRIVATE veyra_widgets_basic)
add_test(NAME toggle_appearance_behavior COMMAND veyra_toggle_appearance_test)

add_executable(veyra_editable_text_test tests/editable_text_test.cpp)
target_link_libraries(veyra_editable_text_test PRIVATE veyra_behaviors)
add_test(NAME editable_text_behavior COMMAND veyra_editable_text_test)

add_executable(veyra_selection_typeahead_test tests/selection_typeahead_test.cpp)
target_link_libraries(veyra_selection_typeahead_test PRIVATE veyra_behaviors)
add_test(NAME selection_typeahead_behavior COMMAND veyra_selection_typeahead_test)

add_executable(veyra_scroll_controller_test tests/scroll_controller_test.cpp)
target_link_libraries(veyra_scroll_controller_test PRIVATE veyra_behaviors)
add_test(NAME scroll_controller_behavior COMMAND veyra_scroll_controller_test)

add_executable(veyra_text_area_composition_test tests/text_area_composition_test.cpp)
target_link_libraries(veyra_text_area_composition_test PRIVATE veyra_behaviors)
add_test(NAME text_area_composition_behavior COMMAND veyra_text_area_composition_test)

add_executable(veyra_indeterminate_progress_test tests/indeterminate_progress_test.cpp)
target_link_libraries(veyra_indeterminate_progress_test PRIVATE veyra_behaviors)
add_test(NAME indeterminate_progress_behavior COMMAND veyra_indeterminate_progress_test)

add_executable(veyra_textfield_appearance_test tests/textfield_appearance_test.cpp)
target_link_libraries(veyra_textfield_appearance_test PRIVATE veyra_widgets_basic)
add_test(NAME textfield_appearance_behavior COMMAND veyra_textfield_appearance_test)

add_executable(veyra_gallery examples/gallery.cpp)
target_link_libraries(veyra_gallery PRIVATE veyra_widgets_basic veyra_behaviors veyra)

add_executable(veyra_gallery_test tests/gallery_test.cpp)
target_link_libraries(veyra_gallery_test PRIVATE veyra_widgets_basic veyra_behaviors veyra)
add_test(NAME gallery_behavior COMMAND veyra_gallery_test)

find_package(Threads REQUIRED)
add_executable(veyra_chat_message_change_queue_test tests/chat_message_change_queue_test.cpp)
target_link_libraries(veyra_chat_message_change_queue_test PRIVATE veyra Threads::Threads)
add_test(NAME chat_message_change_queue_behavior COMMAND veyra_chat_message_change_queue_test)

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

add_executable(veyra_frame_scheduler_test tests/frame_scheduler_test.cpp)
target_link_libraries(veyra_frame_scheduler_test PRIVATE veyra)
add_test(NAME frame_scheduler_behavior COMMAND veyra_frame_scheduler_test)

add_executable(veyra_semantics_test tests/semantics_test.cpp)
target_link_libraries(veyra_semantics_test PRIVATE veyra)
add_test(NAME semantics_behavior COMMAND veyra_semantics_test)

add_executable(veyra_shortcut_map_test tests/shortcut_map_test.cpp)
target_link_libraries(veyra_shortcut_map_test PRIVATE veyra)
add_test(NAME shortcut_map_behavior COMMAND veyra_shortcut_map_test)

add_executable(veyra_platform_window_test tests/platform_window_test.cpp)
target_link_libraries(veyra_platform_window_test PRIVATE veyra)
add_test(NAME platform_window_behavior COMMAND veyra_platform_window_test)

add_executable(veyra_window_router_test tests/window_router_test.cpp)
target_link_libraries(veyra_window_router_test PRIVATE veyra)
add_test(NAME window_router_behavior COMMAND veyra_window_router_test)

add_executable(veyra_fake_services_test tests/fake_services_test.cpp)
target_link_libraries(veyra_fake_services_test PRIVATE veyra)
add_test(NAME fake_services_behavior COMMAND veyra_fake_services_test)

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

add_executable(veyra_scroll_axis_test tests/scroll_axis_test.cpp)
target_link_libraries(veyra_scroll_axis_test PRIVATE veyra)
add_test(NAME scroll_axis_behavior COMMAND veyra_scroll_axis_test)

add_executable(veyra_breakpoint_test tests/breakpoint_test.cpp)
target_link_libraries(veyra_breakpoint_test PRIVATE veyra)
add_test(NAME breakpoint_behavior COMMAND veyra_breakpoint_test)

add_executable(veyra_container_query_test tests/container_query_test.cpp)
target_link_libraries(veyra_container_query_test PRIVATE veyra)
add_test(NAME container_query_behavior COMMAND veyra_container_query_test)

add_executable(veyra_viewport_test tests/viewport_test.cpp)
target_link_libraries(veyra_viewport_test PRIVATE veyra)
add_test(NAME viewport_behavior COMMAND veyra_viewport_test)

add_executable(veyra_toolbar_overflow_test tests/toolbar_overflow_test.cpp)
target_link_libraries(veyra_toolbar_overflow_test PRIVATE veyra)
add_test(NAME toolbar_overflow_behavior COMMAND veyra_toolbar_overflow_test)

add_executable(veyra_layout_measure_test tests/layout_measure_test.cpp)
target_link_libraries(veyra_layout_measure_test PRIVATE veyra)
add_test(NAME layout_measure_behavior COMMAND veyra_layout_measure_test)

add_executable(veyra_image_fit_test tests/image_fit_test.cpp)
target_link_libraries(veyra_image_fit_test PRIVATE veyra)
add_test(NAME image_fit_behavior COMMAND veyra_image_fit_test)

add_executable(veyra_image_controller_test tests/image_controller_test.cpp)
target_link_libraries(veyra_image_controller_test PRIVATE veyra)
add_test(NAME image_controller_behavior COMMAND veyra_image_controller_test)

add_executable(veyra_software_renderer_test tests/software_renderer_test.cpp)
target_link_libraries(veyra_software_renderer_test PRIVATE veyra)
add_test(NAME software_renderer_behavior COMMAND veyra_software_renderer_test)

add_executable(
 veyra_software_renderer_image_sampling_test
 tests/software_renderer_image_sampling_test.cpp
)
target_link_libraries(veyra_software_renderer_image_sampling_test PRIVATE veyra)
add_test(
 NAME software_renderer_image_sampling_behavior
 COMMAND veyra_software_renderer_image_sampling_test
)

add_executable(
 veyra_software_renderer_rounded_rect_test
 tests/software_renderer_rounded_rect_test.cpp
)
target_link_libraries(veyra_software_renderer_rounded_rect_test PRIVATE veyra)
add_test(
 NAME software_renderer_rounded_rect_behavior
 COMMAND veyra_software_renderer_rounded_rect_test
)

add_executable(veyra_software_renderer_snapshot_test tests/software_renderer_snapshot_test.cpp)
target_link_libraries(veyra_software_renderer_snapshot_test PRIVATE veyra)
add_test(NAME software_renderer_snapshot COMMAND veyra_software_renderer_snapshot_test)

add_executable(veyra_software_renderer_line_test tests/software_renderer_line_test.cpp)
target_link_libraries(veyra_software_renderer_line_test PRIVATE veyra)
add_test(NAME software_renderer_line_behavior COMMAND veyra_software_renderer_line_test)

add_executable(veyra_software_renderer_translation_test tests/software_renderer_translation_test.cpp)
target_link_libraries(veyra_software_renderer_translation_test PRIVATE veyra)
add_test(NAME software_renderer_translation_behavior COMMAND veyra_software_renderer_translation_test)

add_executable(veyra_software_renderer_path_test tests/software_renderer_path_test.cpp)
target_link_libraries(veyra_software_renderer_path_test PRIVATE veyra)
add_test(NAME software_renderer_path_behavior COMMAND veyra_software_renderer_path_test)

add_executable(veyra_damage_tracker_test tests/damage_tracker_test.cpp)
target_link_libraries(veyra_damage_tracker_test PRIVATE veyra)
add_test(NAME damage_tracker_behavior COMMAND veyra_damage_tracker_test)

add_executable(veyra_display_batches_test tests/display_batches_test.cpp)
target_link_libraries(veyra_display_batches_test PRIVATE veyra)
add_test(NAME display_batches_behavior COMMAND veyra_display_batches_test)

add_executable(veyra_scroll_shift_test tests/scroll_shift_test.cpp)
target_link_libraries(veyra_scroll_shift_test PRIVATE veyra)
add_test(NAME scroll_shift_behavior COMMAND veyra_scroll_shift_test)

add_executable(veyra_scroll_shift_verify_test tests/scroll_shift_verify_test.cpp)
target_link_libraries(veyra_scroll_shift_verify_test PRIVATE veyra)
add_test(NAME scroll_shift_verify_behavior COMMAND veyra_scroll_shift_verify_test)

add_executable(veyra_inspector_trace_test tests/inspector_trace_test.cpp)
target_link_libraries(veyra_inspector_trace_test PRIVATE veyra)
add_test(NAME inspector_trace_behavior COMMAND veyra_inspector_trace_test)

add_executable(veyra_printing_test tests/printing_test.cpp)
target_link_libraries(veyra_printing_test PRIVATE veyra)
add_test(NAME printing_behavior COMMAND veyra_printing_test)

add_executable(veyra_glyph_mask_cache_test tests/glyph_mask_cache_test.cpp)
target_link_libraries(veyra_glyph_mask_cache_test PRIVATE veyra)
add_test(NAME glyph_mask_cache_behavior COMMAND veyra_glyph_mask_cache_test)

add_executable(veyra_glyph_image_cache_test tests/glyph_image_cache_test.cpp)
target_link_libraries(veyra_glyph_image_cache_test PRIVATE veyra)
add_test(NAME glyph_image_cache_behavior COMMAND veyra_glyph_image_cache_test)

add_executable(veyra_stb_rect_pack_test tests/stb_rect_pack_test.cpp)
target_include_directories(veyra_stb_rect_pack_test PRIVATE third_party/stb)
add_test(NAME stb_rect_pack_behavior COMMAND veyra_stb_rect_pack_test)

add_executable(veyra_stb_sprintf_test tests/stb_sprintf_test.cpp)
target_include_directories(veyra_stb_sprintf_test PRIVATE third_party/stb)
add_test(NAME stb_sprintf_behavior COMMAND veyra_stb_sprintf_test)

add_executable(veyra_text_editor_buffer_test tests/text_editor_buffer_test.cpp)
target_link_libraries(veyra_text_editor_buffer_test PRIVATE veyra)
add_test(NAME text_editor_buffer_behavior COMMAND veyra_text_editor_buffer_test)

add_executable(veyra_text_editor_navigation_test tests/text_editor_navigation_test.cpp)
target_link_libraries(veyra_text_editor_navigation_test PRIVATE veyra)
add_test(NAME text_editor_navigation_behavior COMMAND veyra_text_editor_navigation_test)

add_executable(veyra_text_editor_workload_test tests/text_editor_workload_test.cpp)
target_link_libraries(veyra_text_editor_workload_test PRIVATE veyra_behaviors)
add_test(NAME text_editor_workload_behavior COMMAND veyra_text_editor_workload_test)

add_executable(veyra_allocation_audit_test tests/allocation_audit_test.cpp)
target_link_libraries(veyra_allocation_audit_test PRIVATE veyra veyra_behaviors)
add_test(NAME allocation_audit_behavior COMMAND veyra_allocation_audit_test)

if(VEYRA_WITH_FILESYSTEM)
 target_link_libraries(veyra_allocation_audit_test PRIVATE veyra_filesystem)
 target_compile_definitions(veyra_allocation_audit_test PRIVATE VEYRA_AUDIT_FILESYSTEM=1)
endif()

if(VEYRA_WITH_TEXT)
 target_link_libraries(veyra_allocation_audit_test PRIVATE veyra_text)
 target_compile_definitions(veyra_allocation_audit_test PRIVATE VEYRA_AUDIT_TEXT=1)
endif()

add_executable(veyra_animation_timeline_test tests/animation_timeline_test.cpp)
target_link_libraries(veyra_animation_timeline_test PRIVATE veyra)
add_test(NAME animation_timeline_behavior COMMAND veyra_animation_timeline_test)

add_executable(veyra_animation_interpolation_test tests/animation_interpolation_test.cpp)
target_link_libraries(veyra_animation_interpolation_test PRIVATE veyra)
add_test(NAME animation_interpolation_behavior COMMAND veyra_animation_interpolation_test)

add_executable(veyra_animation_frame_cache_test tests/animation_frame_cache_test.cpp)
target_link_libraries(veyra_animation_frame_cache_test PRIVATE veyra)
add_test(NAME animation_frame_cache_behavior COMMAND veyra_animation_frame_cache_test)

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
 add_executable(veyra_node_tree_dirty_nodes_bench bench/node_tree_dirty_nodes.cpp)
 target_link_libraries(veyra_node_tree_dirty_nodes_bench PRIVATE veyra)
 add_executable(veyra_hit_test_bench bench/hit_test.cpp)
 target_link_libraries(veyra_hit_test_bench PRIVATE veyra)
 add_executable(veyra_software_renderer_bench bench/software_renderer.cpp)
 target_link_libraries(veyra_software_renderer_bench PRIVATE veyra)
 add_executable(veyra_damage_tracker_bench bench/damage_tracker.cpp)
 target_link_libraries(veyra_damage_tracker_bench PRIVATE veyra)
 add_executable(veyra_glyph_mask_cache_bench bench/glyph_mask_cache.cpp)
 target_link_libraries(veyra_glyph_mask_cache_bench PRIVATE veyra)
 add_executable(veyra_glyph_image_cache_bench bench/glyph_image_cache.cpp)
 target_link_libraries(veyra_glyph_image_cache_bench PRIVATE veyra)
 add_executable(veyra_text_editor_buffer_bench bench/text_editor_buffer.cpp)
 target_link_libraries(veyra_text_editor_buffer_bench PRIVATE veyra)
 add_executable(veyra_animation_timeline_bench bench/animation_timeline.cpp)
 target_link_libraries(veyra_animation_timeline_bench PRIVATE veyra)
 add_executable(veyra_chat_row_height_cache_bench bench/chat_row_height_cache.cpp)
 target_link_libraries(veyra_chat_row_height_cache_bench PRIVATE veyra)
 add_executable(veyra_chat_message_change_queue_bench bench/chat_message_change_queue.cpp)
 target_link_libraries(veyra_chat_message_change_queue_bench PRIVATE veyra Threads::Threads)
 add_executable(veyra_file_list_bench bench/file_list.cpp)
 target_link_libraries(veyra_file_list_bench PRIVATE veyra)
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
 target_compile_definitions(veyra_node_tree_dirty_nodes_bench PRIVATE
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
 target_compile_definitions(veyra_damage_tracker_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_glyph_mask_cache_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_glyph_image_cache_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_text_editor_buffer_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_animation_timeline_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_chat_row_height_cache_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_chat_message_change_queue_bench PRIVATE
  "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
  "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
  "VEYRA_BUILD_CONFIG=\"${veyra_build_config}\""
 )
 target_compile_definitions(veyra_file_list_bench PRIVATE
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
 add_test(NAME node_tree_dirty_nodes_benchmark_smoke COMMAND veyra_node_tree_dirty_nodes_bench)
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
  NAME software_renderer_scaled_image_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --scaled-image
 )
 add_test(
  NAME software_renderer_linear_image_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --linear-image
 )
 add_test(
  NAME software_renderer_faded_image_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --faded-image
 )
 add_test(
  NAME software_renderer_border_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --border
 )
 add_test(
  NAME software_renderer_rounded_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --rounded
 )
 add_test(
  NAME software_renderer_rounded_border_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --rounded-border
 )
 add_test(
  NAME software_renderer_line_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --line
 )
 add_test(
  NAME software_renderer_polyline_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --polyline
 )
 add_test(
  NAME software_renderer_translation_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --translated
 )
 add_test(
  NAME software_renderer_path_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --path
 )
 add_test(
  NAME software_renderer_curve_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --curve
 )
 add_test(
  NAME software_renderer_winding_benchmark_smoke
  COMMAND veyra_software_renderer_bench --width 64 --height 48 --frames 20 --warmup 2 --winding
 )
 add_test(NAME damage_tracker_benchmark_smoke COMMAND veyra_damage_tracker_bench)
 add_test(
  NAME glyph_mask_cache_benchmark_smoke
  COMMAND veyra_glyph_mask_cache_bench --glyphs 32 --queries 100 --warmup 10
 )
 add_test(
  NAME glyph_image_cache_benchmark_smoke
  COMMAND veyra_glyph_image_cache_bench
 )
 add_test(
  NAME animation_timeline_benchmark_smoke
  COMMAND veyra_animation_timeline_bench
 )
 add_test(
  NAME chat_row_height_cache_benchmark_smoke
  COMMAND veyra_chat_row_height_cache_bench
 )
 add_test(
  NAME chat_message_change_queue_benchmark_smoke
  COMMAND veyra_chat_message_change_queue_bench --events 10000
 )
 add_test(
  NAME text_editor_buffer_benchmark_smoke
  COMMAND veyra_text_editor_buffer_bench --bytes 1024 --queries 100 --warmup 10
 )
 add_test(
  NAME file_list_benchmark_smoke
  COMMAND veyra_file_list_bench --entries 10000 --warmup 2 --queries 10
 )
endif()
