if(VEYRA_WITH_FILESYSTEM)
 find_package(Threads REQUIRED)
 add_library(veyra_filesystem STATIC
  src/files/enumeration/directory_enumerator.cpp
  src/files/listing/directory_history.cpp
  src/files/enumeration/directory_watcher.cpp
  src/files/shell/file_shell.cpp
  src/files/listing/file_metadata.cpp
  src/files/thumbnails/file_thumbnail_queue.cpp
 )
 target_include_directories(veyra_filesystem PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_filesystem PUBLIC veyra_files veyra_platform veyra_render Threads::Threads)
 if(WIN32)
  target_link_libraries(veyra_filesystem PUBLIC ole32 shell32)
 endif()
 target_compile_features(veyra_filesystem PUBLIC cxx_std_23)

 veyra_add_test(NAME directory_enumerator_behavior TARGET veyra_directory_enumerator_test SOURCES tests/directory_enumerator_test.cpp LIBS veyra_filesystem)
 veyra_add_test(NAME directory_history_behavior TARGET veyra_directory_history_test SOURCES tests/directory_history_test.cpp LIBS veyra_filesystem)

 if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  veyra_add_test(NAME directory_watcher_behavior TARGET veyra_directory_watcher_test SOURCES tests/directory_watcher_test.cpp LIBS veyra_filesystem)
  veyra_add_test(NAME file_shell_behavior TARGET veyra_file_shell_test SOURCES tests/file_shell_test.cpp LIBS veyra_filesystem)
 endif()

 veyra_add_test(NAME file_thumbnail_queue_behavior TARGET veyra_file_thumbnail_queue_test SOURCES tests/file_thumbnail_queue_test.cpp LIBS veyra_filesystem)
 veyra_add_test(NAME file_list_refresh_behavior TARGET veyra_file_list_refresh_test SOURCES tests/file_list_refresh_test.cpp LIBS veyra_filesystem)

 if(VEYRA_BUILD_BENCH)
  veyra_add_test(NAME directory_enumerator_benchmark_smoke TARGET veyra_directory_enumerator_bench SOURCES bench/directory_enumerator.cpp LIBS veyra_filesystem BENCH_DEFINES ARGS --entries 64 --warmup 0 --queries 1)
 endif()
endif()

if(VEYRA_WITH_SDL3)
 if(NOT TARGET SDL3::SDL3)
  message(FATAL_ERROR "VEYRA_WITH_SDL3=ON but SDL3::SDL3 is unavailable")
 endif()
 add_library(veyra_sdl3 STATIC src/platform/sdl3/sdl3_window.cpp)
 target_include_directories(
  veyra_sdl3
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_sdl3 PUBLIC veyra_platform veyra_render SDL3::SDL3)
 target_compile_features(veyra_sdl3 PUBLIC cxx_std_23)

 veyra_add_test(NAME sdl3_window_behavior TARGET veyra_sdl3_window_test SOURCES tests/sdl3_window_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")

 target_sources(veyra_sdl3 PRIVATE src/platform/sdl3/sdl3_translate.cpp)
 target_sources(veyra_sdl3 PRIVATE src/platform/sdl3/sdl3_gpu_present.cpp)

 veyra_add_test(NAME sdl3_translate_behavior TARGET veyra_sdl3_translate_test SOURCES tests/sdl3_translate_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
 veyra_add_test(NAME sdl3_gamepad_behavior TARGET veyra_sdl3_gamepad_test SOURCES tests/sdl3_gamepad_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
 veyra_add_test(NAME sdl3_gpu_present_behavior TARGET veyra_sdl3_gpu_present_test SOURCES tests/sdl3_gpu_present_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
 veyra_add_test(NAME sdl3_gpu_present_windowed TARGET veyra_sdl3_gpu_present_test NO_BUILD ENVIRONMENT "VEYRA_GPU_WINDOWED=1;SDL_VIDEODRIVER=x11")

 if(VEYRA_GPU_SDL)
  set(veyra_sdl_gpu_spv_headers "")
  set(veyra_sdl_gpu_include_dir "")
  if(VEYRA_HERMETIC)
   set(veyra_sdl_gpu_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv/sdl_gpu")
   foreach(shader_spec "rect_sdl:RectSdl" "rounded_rect_sdl:RoundedRectSdl" "image_sdl:ImageSdl" "mask_sdl:MaskSdl" "stroke_sdl:StrokeSdl" "rounded_stroke_sdl:RoundedStrokeSdl" "glyph_sdl:GlyphSdl")
    string(REPLACE ":" ";" shader_parts "${shader_spec}")
    list(GET shader_parts 0 shader_name)
    foreach(stage Vert Frag)
     string(TOLOWER "${stage}" stage_lower)
     set(shader_header "${veyra_sdl_gpu_include_dir}/${shader_name}_${stage_lower}_spv.hpp")
     if(NOT EXISTS "${shader_header}")
      message(FATAL_ERROR "VEYRA_HERMETIC: missing precompiled shader ${shader_header}")
     endif()
     list(APPEND veyra_sdl_gpu_spv_headers "${shader_header}")
    endforeach()
   endforeach()
  else()
   find_program(VEYRA_SDL_GLSLANG_VALIDATOR glslangValidator)
   if(NOT VEYRA_SDL_GLSLANG_VALIDATOR)
    message(STATUS "Veyra: glslangValidator missing; skipping the SDL_GPU consumer, software rendering remains available")
   else()
    set(veyra_sdl_gpu_gen "${CMAKE_CURRENT_BINARY_DIR}/sdl_gpu_gen")
    file(MAKE_DIRECTORY "${veyra_sdl_gpu_gen}")
    set(veyra_sdl_gpu_include_dir "${veyra_sdl_gpu_gen}")
    foreach(shader_spec "rect_sdl:RectSdl" "rounded_rect_sdl:RoundedRectSdl" "image_sdl:ImageSdl" "mask_sdl:MaskSdl" "stroke_sdl:StrokeSdl" "rounded_stroke_sdl:RoundedStrokeSdl" "glyph_sdl:GlyphSdl")
     string(REPLACE ":" ";" shader_parts "${shader_spec}")
     list(GET shader_parts 0 shader_name)
     list(GET shader_parts 1 shader_prefix)
     foreach(stage Vert Frag)
      string(TOLOWER "${stage}" stage_lower)
      set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/src/render/gpu/sdl/shaders/${shader_name}.${stage_lower}")
      set(shader_spv "${veyra_sdl_gpu_gen}/${shader_name}.${stage_lower}.spv")
      set(shader_header "${veyra_sdl_gpu_gen}/${shader_name}_${stage_lower}_spv.hpp")
      add_custom_command(
       OUTPUT "${shader_spv}"
       COMMAND "${VEYRA_SDL_GLSLANG_VALIDATOR}" -V "${shader_source}" -o "${shader_spv}"
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
      list(APPEND veyra_sdl_gpu_spv_headers "${shader_header}")
     endforeach()
    endforeach()
   endif()
  endif()

  if(veyra_sdl_gpu_spv_headers)
   add_library(veyra_sdl_gpu STATIC
    src/render/gpu/sdl/sdl_gpu_renderer.cpp
    src/render/gpu/sdl/sdl_gpu_emit.cpp
    src/render/gpu/sdl/sdl_gpu_stroke.cpp
    src/render/gpu/sdl/sdl_gpu_path.cpp
    src/render/gpu/sdl/sdl_gpu_frame.cpp
    src/render/gpu/sdl/sdl_gpu_pipelines.cpp
    src/render/gpu/sdl/sdl_gpu_textures.cpp
    ${veyra_sdl_gpu_spv_headers}
   )
   target_include_directories(veyra_sdl_gpu PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
   )
   target_include_directories(veyra_sdl_gpu PRIVATE
    "${veyra_sdl_gpu_include_dir}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/cpu"
   )
   target_link_libraries(veyra_sdl_gpu PUBLIC veyra_sdl3 veyra_platform veyra_render veyra_text SDL3::SDL3)
   target_compile_features(veyra_sdl_gpu PUBLIC cxx_std_23)

   veyra_add_test(NAME sdl_gpu_rect_fill_parity TARGET veyra_sdl_gpu_rect_fill_parity_test SOURCES tests/sdl_gpu_rect_fill_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_rounded_rect_parity TARGET veyra_sdl_gpu_rounded_rect_parity_test SOURCES tests/sdl_gpu_rounded_rect_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_image_parity TARGET veyra_sdl_gpu_image_parity_test SOURCES tests/sdl_gpu_image_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_stroke_parity TARGET veyra_sdl_gpu_stroke_parity_test SOURCES tests/sdl_gpu_stroke_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_mask_parity TARGET veyra_sdl_gpu_mask_parity_test SOURCES tests/sdl_gpu_mask_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_glyph_parity TARGET veyra_sdl_gpu_glyph_parity_test SOURCES tests/sdl_gpu_glyph_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_clip_parity TARGET veyra_sdl_gpu_clip_parity_test SOURCES tests/sdl_gpu_clip_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
   veyra_add_test(NAME sdl_gpu_path_parity TARGET veyra_sdl_gpu_path_parity_test SOURCES tests/sdl_gpu_path_parity_test.cpp LIBS veyra_sdl_gpu SKIP_RETURN_CODE 77)
  endif()
 endif()
endif()

if(VEYRA_WITH_GLFW)
 veyra_dependency_glfw()
 if(TARGET glfw)
  set(veyra_glfw_target glfw)
 elseif(TARGET glfw3::glfw)
  set(veyra_glfw_target glfw3::glfw)
 else()
  message(FATAL_ERROR "glfw3 was found but did not define a supported CMake target")
 endif()
 add_library(veyra_glfw STATIC src/platform/glfw/glfw_window.cpp)
 target_include_directories(veyra_glfw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_glfw PUBLIC veyra_platform veyra_render PRIVATE ${veyra_glfw_target})
 target_compile_features(veyra_glfw PUBLIC cxx_std_23)

 veyra_add_test(NAME glfw_window_behavior TARGET veyra_glfw_window_test SOURCES tests/glfw_window_test.cpp LIBS veyra_glfw ${veyra_glfw_target} SKIP_RETURN_CODE 77)
endif()
