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
 src/render/retained_scroll.cpp
 src/render/software_command_validation.cpp
 src/render/software_path.cpp
 src/render/glyph_atlas.cpp
 src/render/software_rounded_rect.cpp
 src/render/software_image.cpp
 src/render/shadow.cpp
 src/render/e3_scenes.cpp
 src/render/display_command_hash.cpp
 src/render/display_equivalence.cpp
 src/text/glyph_mask_cache.cpp
 src/text/glyph_image_cache.cpp
 src/text/syntax_spans.cpp
 src/text/line_diff.cpp
 src/text/text_edit_history.cpp
 src/text/text_editor_composition.cpp
 src/text/text_editor_buffer.cpp
 src/render/software_renderer.cpp
 src/platform/null_window.cpp
 src/platform/fake_services.cpp
 src/platform/window_router.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb>)
target_compile_features(veyra PUBLIC cxx_std_23)

add_library(veyra_behaviors STATIC src/behaviors/pressable.cpp src/behaviors/toggle.cpp src/behaviors/selection.cpp src/behaviors/selection_typeahead.cpp src/behaviors/scroll.cpp src/behaviors/breadcrumb.cpp src/behaviors/tooltip.cpp src/behaviors/editable_text.cpp src/behaviors/overlay.cpp src/behaviors/toast.cpp src/behaviors/disclosure.cpp src/behaviors/drag_drop.cpp src/behaviors/datagrid.cpp src/behaviors/datagrid_cursor.cpp src/behaviors/cell_range.cpp src/behaviors/range.cpp src/behaviors/radiogroup.cpp src/behaviors/tabs.cpp src/behaviors/menu.cpp src/behaviors/menubar.cpp src/behaviors/combobox.cpp src/behaviors/keybinding.cpp src/behaviors/file_operation_queue.cpp src/behaviors/command_palette.cpp src/behaviors/progress.cpp src/behaviors/log_feed.cpp src/behaviors/icon_view.cpp)
target_include_directories(veyra_behaviors PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(veyra_behaviors PUBLIC veyra)
target_compile_features(veyra_behaviors PUBLIC cxx_std_23)

add_library(veyra_data STATIC src/data/list_pipeline.cpp src/data/server_rows.cpp)
target_include_directories(veyra_data PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra_data PUBLIC cxx_std_23)

add_library(veyra_style STATIC src/style/theme.cpp)
target_include_directories(veyra_style PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(veyra_style PUBLIC cxx_std_23)

add_library(
    veyra_widgets_basic
    STATIC
    src/widgets/basic/button.cpp
    src/widgets/basic/slider.cpp
    src/widgets/basic/toggle.cpp
    src/widgets/basic/textfield.cpp
    src/widgets/basic/text_area.cpp
    src/widgets/basic/radio.cpp
    src/widgets/data/virtual_list.cpp
    src/widgets/data/log_feed.cpp
    src/widgets/data/icon_view.cpp
    src/widgets/data/data_grid.cpp
    src/widgets/data/tree_view.cpp
    src/widgets/data/progress_bar.cpp
    src/widgets/overlay/popover.cpp
    src/widgets/overlay/toast_card.cpp
    src/widgets/overlay/tooltip_card.cpp
    src/widgets/data/menu_list.cpp
    src/widgets/data/menubar.cpp
)
target_include_directories(veyra_widgets_basic PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(veyra_widgets_basic PUBLIC veyra_behaviors veyra_style)
target_compile_features(veyra_widgets_basic PUBLIC cxx_std_23)

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
