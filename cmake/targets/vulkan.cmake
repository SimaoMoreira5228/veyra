if(VEYRA_WITH_VULKAN)
 find_package(Vulkan QUIET)
 find_program(VEYRA_GLSLANG_VALIDATOR glslangValidator)
 if(NOT Vulkan_FOUND OR NOT VEYRA_GLSLANG_VALIDATOR)
  message(STATUS "Veyra: Vulkan headers/loader or glslangValidator missing, skipping offscreen target")
 else()
  set(veyra_vulkan_gen "${CMAKE_CURRENT_BINARY_DIR}/vulkan_gen")
  file(MAKE_DIRECTORY "${veyra_vulkan_gen}")
  set(veyra_vulkan_spv_headers "")
  foreach(shader_spec "rect:Rect" "rounded_rect:RoundedRect" "image:Image" "mask:Mask" "stroke:Stroke" "rounded_stroke:RoundedStroke" "glyph:Glyph")
   string(REPLACE ":" ";" shader_parts "${shader_spec}")
   list(GET shader_parts 0 shader_name)
   list(GET shader_parts 1 shader_prefix)
   foreach(stage Vert Frag)
    string(TOLOWER "${stage}" stage_lower)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/src/render/vulkan/vulkan/shaders/${shader_name}.${stage_lower}")
    set(shader_spv "${veyra_vulkan_gen}/${shader_name}.${stage_lower}.spv")
    set(shader_header "${veyra_vulkan_gen}/${shader_name}_${stage_lower}_spv.hpp")
    add_custom_command(
     OUTPUT "${shader_spv}"
     COMMAND "${VEYRA_GLSLANG_VALIDATOR}" -V "${shader_source}" -o "${shader_spv}"
     DEPENDS "${shader_source}"
     COMMENT "Veyra: compiling ${shader_name}.${stage_lower} to SPIR-V"
    )
    add_custom_command(
     OUTPUT "${shader_header}"
     COMMAND "${CMAKE_COMMAND}" -DINPUT="${shader_spv}" -DOUTPUT="${shader_header}"
      "-DVAR=k${shader_prefix}${stage}Spv" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
     DEPENDS "${shader_spv}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
     COMMENT "Veyra: embedding ${shader_name}.${stage_lower}.spv"
    )
    list(APPEND veyra_vulkan_spv_headers "${shader_header}")
   endforeach()
  endforeach()
  add_library(veyra_vulkan STATIC
   src/render/vulkan_context.cpp
   src/render/vulkan_internal.cpp
   src/render/vulkan_texture_cache.cpp
   src/render/vulkan_texture_upload.cpp
   src/render/vulkan_mask_cache.cpp
   src/render/vulkan_pipeline_assembly.cpp
   src/render/vulkan_rect_pipeline.cpp
    src/render/vulkan_frame_batch.cpp
   src/render/vulkan_frame_emit.cpp
   src/render/vulkan_frame_stroke.cpp
   src/render/vulkan_frame_transfer.cpp
   src/render/vulkan_rect_frame.cpp
   ${veyra_vulkan_spv_headers}
  )
  target_include_directories(veyra_vulkan PUBLIC
   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_include_directories(veyra_vulkan PRIVATE "${veyra_vulkan_gen}")
  target_link_libraries(veyra_vulkan PUBLIC veyra Vulkan::Vulkan)
  target_compile_features(veyra_vulkan PUBLIC cxx_std_23)

  veyra_add_test(NAME vulkan_rect_fill_parity TARGET veyra_vulkan_rect_fill_parity_test SOURCES tests/vulkan_rect_fill_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_rounded_rect_parity TARGET veyra_vulkan_rounded_rect_parity_test SOURCES tests/vulkan_rounded_rect_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_rect_validation_parity TARGET veyra_vulkan_rect_validation_parity_test SOURCES tests/vulkan_rect_validation_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_e3_scenes TARGET veyra_vulkan_e3_scenes_test SOURCES tests/vulkan_e3_scenes_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_image_parity TARGET veyra_vulkan_image_parity_test SOURCES tests/vulkan_image_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_mask_parity TARGET veyra_vulkan_mask_parity_test SOURCES tests/vulkan_mask_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_stroke_parity TARGET veyra_vulkan_stroke_parity_test SOURCES tests/vulkan_stroke_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_rounded_stroke_parity TARGET veyra_vulkan_rounded_stroke_parity_test SOURCES tests/vulkan_rounded_stroke_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_path_parity TARGET veyra_vulkan_path_parity_test SOURCES tests/vulkan_path_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_clip_parity TARGET veyra_vulkan_clip_parity_test SOURCES tests/vulkan_clip_parity_test.cpp LIBS veyra_vulkan)

  if(VEYRA_WITH_TEXT)
   veyra_add_test(NAME vulkan_glyph_parity TARGET veyra_vulkan_glyph_parity_test SOURCES tests/vulkan_glyph_parity_test.cpp LIBS veyra_vulkan veyra_text)
   veyra_add_test(NAME vulkan_glyph_atlas_parity TARGET veyra_vulkan_glyph_atlas_parity_test SOURCES tests/vulkan_glyph_atlas_parity_test.cpp LIBS veyra_vulkan veyra_text)
   if(VEYRA_WITH_FREETYPE)
    veyra_add_test(NAME vulkan_raster_atlas_parity TARGET veyra_vulkan_raster_atlas_parity_test SOURCES tests/vulkan_raster_atlas_parity_test.cpp LIBS veyra_vulkan veyra_text veyra_freetype)
    veyra_add_test(NAME real_font_text TARGET veyra_real_font_text_test SOURCES tests/real_font_text_test.cpp LIBS veyra_vulkan veyra_text veyra_freetype DEFINES "VEYRA_TESTDATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/testdata\"")
   endif()
  endif()

  veyra_add_test(NAME vulkan_damage_parity TARGET veyra_vulkan_damage_parity_test SOURCES tests/vulkan_damage_parity_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_device_health TARGET veyra_vulkan_device_health_test SOURCES tests/vulkan_device_health_test.cpp LIBS veyra_vulkan)
  veyra_add_test(NAME vulkan_scroll_parity TARGET veyra_vulkan_scroll_parity_test SOURCES tests/vulkan_scroll_parity_test.cpp LIBS veyra_vulkan)

  add_executable(veyra_vulkan_e3_bench bench/vulkan_e3_scenes.cpp)
  target_link_libraries(veyra_vulkan_e3_bench PRIVATE veyra_vulkan)
  target_compile_definitions(veyra_vulkan_e3_bench PRIVATE
   "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
   "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
   "VEYRA_BUILD_CONFIG=\"$<CONFIG>\""
  )
  add_test(
   NAME vulkan_e3_benchmark_smoke
   COMMAND veyra_vulkan_e3_bench --scene shadowed-boxes-600 --frames 2 --warmup 1
  )
  add_test(
   NAME vulkan_e3_benchmark_scroll_smoke
   COMMAND veyra_vulkan_e3_bench --scene row-scroll-1000 --mode scroll --frames 2 --warmup 1
  )
  add_test(
   NAME vulkan_e3_benchmark_damage_smoke
   COMMAND veyra_vulkan_e3_bench --scene dashboard-12 --mode damage --frames 2 --warmup 1
  )
  if(VEYRA_WITH_TEXT)
   add_executable(veyra_vulkan_text_churn_bench bench/vulkan_text_churn.cpp)
   target_link_libraries(veyra_vulkan_text_churn_bench PRIVATE veyra_vulkan veyra_text)
   target_compile_definitions(veyra_vulkan_text_churn_bench PRIVATE
    "VEYRA_BUILD_PLATFORM=\"${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}\""
    "VEYRA_BUILD_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\""
    "VEYRA_BUILD_CONFIG=\"$<CONFIG>\""
   )
   add_test(
    NAME vulkan_text_churn_benchmark_smoke
    COMMAND veyra_vulkan_text_churn_bench --lines 8 --fresh-per-frame 1 --frames 2 --warmup 1
   )
  endif()
 endif()
endif()
