add_library(veyra STATIC
 src/chat/message/message_change_queue.cpp
 src/chat/rows/row_height_cache.cpp
 src/files/listing/file_list.cpp
 src/inspector/trace/trace_recorder.cpp
 src/media/animation/animation_frame_cache.cpp
 src/media/animation/animation_timeline.cpp
 src/core/event/focus_navigation.cpp
 src/core/schedule/frame_scheduler.cpp
 src/core/event/hit_test.cpp
 src/core/tree/node_id_pool.cpp
 src/core/tree/node_tree.cpp
 src/core/semantics/semantics.cpp
 src/core/input/shortcut_map.cpp
 src/core/input/shortcut_sequence.cpp
 src/layout/responsive/breakpoints.cpp
 src/layout/responsive/container_queries.cpp
 src/layout/panels/dock.cpp
 src/layout/flex/flex.cpp
 src/layout/grid/grid.cpp
 src/layout/grid/grid_placement.cpp
 src/layout/image/image_fit.cpp
 src/layout/image/image.cpp
 src/layout/core/measure.cpp
 src/layout/scroll/scroll_axis.cpp
 src/layout/flex/stack.cpp
 src/layout/scroll/viewport.cpp
 src/layout/scroll/virtual_list.cpp
 src/render/damage/damage_tracker.cpp
 src/render/display/display_batches.cpp
 src/render/damage/scroll_shift.cpp
 src/render/damage/retained_scroll.cpp
 src/render/cpu/software_command_validation.cpp
 src/render/cpu/software_path.cpp
 src/render/resource/glyph_atlas.cpp
 src/render/cpu/software_rounded_rect.cpp
 src/render/cpu/software_image.cpp
 src/render/draw/shadow.cpp
 src/render/display/display_command_hash.cpp
 src/render/display/display_equivalence.cpp
 src/render/present/present_choice.cpp
 src/text/editing/editor_viewport.cpp
 src/text/shaping/font_discovery.cpp
 src/text/rendering/glyph_mask_cache.cpp
 src/text/rendering/glyph_image_cache.cpp
 src/text/annotation/syntax_spans.cpp
 src/text/annotation/line_diff.cpp
 src/text/editing/text_edit_history.cpp
 src/text/editing/text_editor_composition.cpp
 src/text/editing/text_editor_buffer.cpp
 src/render/cpu/software_renderer.cpp
 src/platform/headless/null_window.cpp
 src/platform/headless/fake_services.cpp
 src/platform/window_router.cpp
)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(veyra PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb>)
target_compile_features(veyra PUBLIC cxx_std_23)

add_library(veyra_behaviors STATIC src/behaviors/control/pressable.cpp src/behaviors/control/toggle.cpp src/behaviors/selection/selection.cpp src/behaviors/selection/selection_typeahead.cpp src/behaviors/control/scroll.cpp src/behaviors/navigation/breadcrumb.cpp src/behaviors/overlay/tooltip.cpp src/behaviors/input/editable_text.cpp src/behaviors/overlay/overlay.cpp src/behaviors/overlay/toast.cpp src/behaviors/navigation/disclosure.cpp src/behaviors/overlay/drag_drop.cpp src/behaviors/data/datagrid.cpp src/behaviors/selection/datagrid_cursor.cpp src/behaviors/selection/cell_range.cpp src/behaviors/input/range.cpp src/behaviors/input/radiogroup.cpp src/behaviors/selection/tabs.cpp src/behaviors/navigation/menu.cpp src/behaviors/navigation/menubar.cpp src/behaviors/navigation/combobox.cpp src/behaviors/navigation/keybinding.cpp src/behaviors/data/file_operation_queue.cpp src/behaviors/navigation/command_palette.cpp src/behaviors/navigation/focus_chain.cpp src/behaviors/control/progress.cpp src/behaviors/data/log_feed.cpp src/behaviors/data/icon_view.cpp)
target_include_directories(veyra_behaviors PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(veyra_behaviors PUBLIC veyra)
target_compile_features(veyra_behaviors PUBLIC cxx_std_23)

add_library(veyra_data STATIC src/data/pipeline/list_pipeline.cpp src/data/source/server_rows.cpp src/services/shell/shell.cpp)
target_include_directories(veyra_data PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(veyra_data PUBLIC cxx_std_23)

add_library(veyra_style STATIC src/style/theme/theme.cpp src/style/theme/theme_file.cpp)
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

