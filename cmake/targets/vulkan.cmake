if(VEYRA_WITH_VULKAN)
 find_package(Vulkan QUIET)
 if(NOT Vulkan_FOUND)
  message(STATUS "Veyra: Vulkan loader missing, skipping NoGraphicsAPI backend")
 else()
  find_program(VEYRA_NOGA_SLANGC slangc)
  if(NOT VEYRA_NOGA_SLANGC)
   message(STATUS "Veyra: slangc missing, skipping NoGraphicsAPI backend")
  else()
   set(veyra_noga_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/noga_probe")
   file(MAKE_DIRECTORY "${veyra_noga_probe_dir}")
   file(WRITE "${veyra_noga_probe_dir}/probe.slang"
    "[shader(\"fragment\")]\nfloat4 probeMain() : SV_Target0 { return float4(1.0, 0.0, 0.0, 1.0); }\n"
   )
   execute_process(
    COMMAND "${VEYRA_NOGA_SLANGC}" "${veyra_noga_probe_dir}/probe.slang" -target spirv
     -profile spirv_1_5 -emit-spirv-directly -fvk-use-entrypoint-name -entry probeMain
     -stage fragment -o "${veyra_noga_probe_dir}/probe.spv"
    RESULT_VARIABLE veyra_noga_probe_result
    OUTPUT_QUIET
    ERROR_QUIET
   )
   if(NOT veyra_noga_probe_result EQUAL 0)
    message(STATUS "Veyra: system slangc cannot emit SPIR-V, fetching pinned slangc")
    include(FetchContent)
    FetchContent_Declare(
     slang-linux
     URL https://github.com/shader-slang/slang/releases/download/v2026.20/slang-2026.20-linux-x86_64.tar.gz
     URL_HASH SHA256=e863e568c204810d6e8bc31852f056aef6763c55ea9925db2db7affa060347c9
    )
    FetchContent_MakeAvailable(slang-linux)
    set(VEYRA_NOGA_SLANGC "${slang-linux_SOURCE_DIR}/bin/slangc")
   endif()
   find_program(VEYRA_NOGA_SPIRV_VAL spirv-val)
   # NOTE: NoGraphicsAPI declares Vulkan 1.4.357 as its floor, but the pinned
   # HEAD needs newer device-address-command declarations, so fetch 1.4.365.
   set(veyra_noga_vh_dir "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-1.4.365")
   if(NOT EXISTS "${veyra_noga_vh_dir}/include/vulkan/vulkan_core.h")
    file(
     DOWNLOAD
      https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.365.tar.gz
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-1.4.365.tar.gz"
     EXPECTED_HASH SHA256=ed832b3292cce324ebba6522fd2d3004bf1b797fa49bfbcad0b302ff23d56abc
     SHOW_PROGRESS
    )
    file(REMOVE_RECURSE "${veyra_noga_vh_dir}")
    file(
     ARCHIVE_EXTRACT
      INPUT "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-1.4.365.tar.gz"
      DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-extract"
    )
    file(RENAME "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-extract/Vulkan-Headers-1.4.365" "${veyra_noga_vh_dir}")
    file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/vulkan-headers-extract")
   endif()
   set(Vulkan_INCLUDE_DIR "${veyra_noga_vh_dir}/include" CACHE PATH "" FORCE)
   if(TARGET Vulkan::Headers)
    # NOTE: the outer unversioned find_package already created this imported
    # target against system headers, so repoint it after overriding the dir.
    set_target_properties(
     Vulkan::Headers
     PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${veyra_noga_vh_dir}/include"
    )
   endif()
   # NOTE: brief hash ffd9f2...80089 is off by one nibble and matches no upstream
   # object; pin the verified upstream HEAD the reference clone sits on instead.
   FetchContent_Declare(
    NoGraphicsAPI
    GIT_REPOSITORY https://github.com/sebbbi/NoGraphicsAPI.git
    GIT_TAG ffd9f2ab6b7442dcfbe0cb0051d84d0edb078d89
   )
   FetchContent_MakeAvailable(NoGraphicsAPI)
   set(veyra_noga_gen "${CMAKE_CURRENT_BINARY_DIR}/noga_gen")
   file(MAKE_DIRECTORY "${veyra_noga_gen}")
   set(veyra_noga_slang_flags
    -target spirv -profile spirv_1_5 -emit-spirv-directly -fvk-use-entrypoint-name
    -fvk-use-c-layout -matrix-layout-row-major -capability spvDescriptorHeapEXT
    -I "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga"
    -I "${nographicsapi_SOURCE_DIR}/include"
    -I "${nographicsapi_SOURCE_DIR}/utility/include"
   )
   set(veyra_noga_mesh_validate "")
   set(veyra_noga_frag_validate "")
   if(VEYRA_NOGA_SPIRV_VAL)
    set(veyra_noga_mesh_validate
     COMMAND "${VEYRA_NOGA_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout
      "${veyra_noga_gen}/rect.mesh.spv"
    )
    set(veyra_noga_frag_validate
     COMMAND "${VEYRA_NOGA_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout
      "${veyra_noga_gen}/rect.frag.spv"
    )
   endif()
   add_custom_command(
    OUTPUT "${veyra_noga_gen}/rect.mesh.spv"
    COMMAND "${VEYRA_NOGA_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect.slang"
     ${veyra_noga_slang_flags} -capability spvMeshShadingEXT -entry meshMain -stage mesh
     -o "${veyra_noga_gen}/rect.mesh.spv"
    ${veyra_noga_mesh_validate}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect.slang"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect_shared.h"
     "${nographicsapi_SOURCE_DIR}/include/NoGraphicsAPI/shader.slang"
     "${nographicsapi_SOURCE_DIR}/utility/include/NoGraphicsAPIUtility/shader_types.h"
    COMMENT "Veyra: compiling noga rect mesh shader to SPIR-V"
   )
   add_custom_command(
    OUTPUT "${veyra_noga_gen}/rect.frag.spv"
    COMMAND "${VEYRA_NOGA_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect.slang"
     ${veyra_noga_slang_flags} -entry fragmentMain -stage fragment
     -o "${veyra_noga_gen}/rect.frag.spv"
    ${veyra_noga_frag_validate}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect.slang"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/rect_shared.h"
     "${nographicsapi_SOURCE_DIR}/include/NoGraphicsAPI/shader.slang"
     "${nographicsapi_SOURCE_DIR}/utility/include/NoGraphicsAPIUtility/shader_types.h"
    COMMENT "Veyra: compiling noga rect fragment shader to SPIR-V"
   )
   add_custom_command(
    OUTPUT "${veyra_noga_gen}/rect_mesh_spv.hpp"
    COMMAND "${CMAKE_COMMAND}" -DINPUT="${veyra_noga_gen}/rect.mesh.spv"
     -DOUTPUT="${veyra_noga_gen}/rect_mesh_spv.hpp" "-DVAR=kNogaRectMeshSpv"
     "-DNAMESPACE=veyra::noga_detail"
     -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
    DEPENDS "${veyra_noga_gen}/rect.mesh.spv"
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
    COMMENT "Veyra: embedding noga rect mesh SPIR-V"
   )
   add_custom_command(
    OUTPUT "${veyra_noga_gen}/rect_frag_spv.hpp"
    COMMAND "${CMAKE_COMMAND}" -DINPUT="${veyra_noga_gen}/rect.frag.spv"
     -DOUTPUT="${veyra_noga_gen}/rect_frag_spv.hpp" "-DVAR=kNogaRectFragSpv"
     "-DNAMESPACE=veyra::noga_detail"
     -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
    DEPENDS "${veyra_noga_gen}/rect.frag.spv"
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
    COMMENT "Veyra: embedding noga rect fragment SPIR-V"
   )
   set(veyra_noga_spv_headers
    "${veyra_noga_gen}/rect_mesh_spv.hpp"
    "${veyra_noga_gen}/rect_frag_spv.hpp"
   )
   foreach(shader_spec "rect_frame:RectFrame" "rounded_rect_frame:RoundedRectFrame" "image_frame:ImageFrame" "mask_frame:MaskFrame" "stroke_frame:StrokeFrame" "rounded_stroke_frame:RoundedStrokeFrame" "glyph_frame:GlyphFrame")
    string(REPLACE ":" ";" shader_parts "${shader_spec}")
    list(GET shader_parts 0 shader_name)
    list(GET shader_parts 1 shader_prefix)
    foreach(stage_spec "vertexMain:vertex:vert" "fragmentMain:fragment:frag")
     string(REPLACE ":" ";" stage_parts "${stage_spec}")
     list(GET stage_parts 0 stage_entry)
     list(GET stage_parts 1 stage_name)
     list(GET stage_parts 2 stage_short)
     set(shader_spv "${veyra_noga_gen}/${shader_name}.${stage_short}.spv")
     set(shader_header "${veyra_noga_gen}/${shader_name}_${stage_short}_spv.hpp")
     add_custom_command(
      OUTPUT "${shader_spv}"
      COMMAND "${VEYRA_NOGA_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/${shader_name}.slang"
       ${veyra_noga_slang_flags} -entry "${stage_entry}" -stage "${stage_name}"
       -o "${shader_spv}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/${shader_name}.slang"
       "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/frame_common.slang"
       "${CMAKE_CURRENT_SOURCE_DIR}/src/render/noga/frame_shared.h"
       "${nographicsapi_SOURCE_DIR}/include/NoGraphicsAPI/shader.slang"
       "${nographicsapi_SOURCE_DIR}/utility/include/NoGraphicsAPIUtility/shader_types.h"
      COMMENT "Veyra: compiling noga ${shader_name} ${stage_name} shader to SPIR-V"
     )
     if(stage_short STREQUAL "vert")
      set(shader_var "k${shader_prefix}VertSpv")
     else()
      set(shader_var "k${shader_prefix}FragSpv")
     endif()
     add_custom_command(
      OUTPUT "${shader_header}"
      COMMAND "${CMAKE_COMMAND}" -DINPUT="${shader_spv}" -DOUTPUT="${shader_header}"
       "-DVAR=${shader_var}" "-DNAMESPACE=veyra::noga_detail"
       -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
      DEPENDS "${shader_spv}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake/veyra_embed_spirv.cmake"
      COMMENT "Veyra: embedding noga ${shader_name} ${stage_name} SPIR-V"
     )
     list(APPEND veyra_noga_spv_headers "${shader_header}")
    endforeach()
   endforeach()
   add_library(veyra_noga STATIC
    src/render/noga_rect.cpp
    src/render/noga_frame.cpp
    src/render/noga_frame_batch.cpp
    src/render/noga_frame_damage.cpp
    src/render/noga_frame_emit.cpp
    src/render/noga_frame_stroke.cpp
    src/render/noga_frame_submit.cpp
    src/render/noga_frame_upload.cpp
    ${veyra_noga_spv_headers}
   )
   target_include_directories(veyra_noga PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
   )
   target_include_directories(veyra_noga PRIVATE
    "${veyra_noga_gen}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render"
    "${nographicsapi_SOURCE_DIR}/include"
    "${nographicsapi_SOURCE_DIR}/utility/include"
   )
   target_link_libraries(veyra_noga PUBLIC veyra PRIVATE NoGraphicsAPI::NoGraphicsAPI)
   target_compile_features(veyra_noga PUBLIC cxx_std_23)
   veyra_add_test(
    NAME noga_rect_parity
    TARGET veyra_noga_rect_parity_test
    SOURCES tests/noga_rect_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_ordering_parity
    TARGET veyra_noga_ordering_parity_test
    SOURCES tests/noga_ordering_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_rounded_rect_parity
    TARGET veyra_noga_rounded_rect_parity_test
    SOURCES tests/noga_rounded_rect_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_image_parity
    TARGET veyra_noga_image_parity_test
    SOURCES tests/noga_image_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_mask_parity
    TARGET veyra_noga_mask_parity_test
    SOURCES tests/noga_mask_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_stroke_parity
    TARGET veyra_noga_stroke_parity_test
    SOURCES tests/noga_stroke_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_rounded_stroke_parity
    TARGET veyra_noga_rounded_stroke_parity_test
    SOURCES tests/noga_rounded_stroke_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_path_parity
    TARGET veyra_noga_path_parity_test
    SOURCES tests/noga_path_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_clip_parity
    TARGET veyra_noga_clip_parity_test
    SOURCES tests/noga_clip_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_frame_validation_parity
    TARGET veyra_noga_frame_validation_parity_test
    SOURCES tests/noga_frame_validation_parity_test.cpp
    LIBS veyra_noga
   )
   veyra_add_test(
    NAME noga_damage_parity
    TARGET veyra_noga_damage_parity_test
    SOURCES tests/noga_damage_parity_test.cpp
    LIBS veyra_noga
   )
   if(VEYRA_WITH_TEXT)
    veyra_add_test(
     NAME noga_glyph_parity
     TARGET veyra_noga_glyph_parity_test
     SOURCES tests/noga_glyph_parity_test.cpp
     LIBS veyra_noga veyra_text
    )
    veyra_add_test(
     NAME noga_glyph_atlas_parity
     TARGET veyra_noga_glyph_atlas_parity_test
     SOURCES tests/noga_glyph_atlas_parity_test.cpp
     LIBS veyra_noga veyra_text
    )
   endif()
  endif()
 endif()
endif()
