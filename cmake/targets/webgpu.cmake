if(VEYRA_WITH_WEBGPU)
 find_path(
  VEYRA_WEBGPU_INCLUDE_DIR
  NAMES webgpu/webgpu.h
  HINTS "$ENV{VEYRA_WEBGPU_ROOT}" ${VEYRA_WEBGPU_ROOT}
  PATH_SUFFIXES include
  NO_CMAKE_FIND_ROOT_PATH
 )
 if(MINGW)
  find_file(
   VEYRA_WEBGPU_LIBRARY
   NAMES libwgpu_native.a
   HINTS "$ENV{VEYRA_WEBGPU_ROOT}" ${VEYRA_WEBGPU_ROOT}
   PATH_SUFFIXES lib
   NO_CMAKE_FIND_ROOT_PATH
  )
 else()
  find_library(
   VEYRA_WEBGPU_LIBRARY
   NAMES wgpu_native
   HINTS "$ENV{VEYRA_WEBGPU_ROOT}" ${VEYRA_WEBGPU_ROOT}
   PATH_SUFFIXES lib
   NO_CMAKE_FIND_ROOT_PATH
  )
 endif()
 if(NOT VEYRA_WEBGPU_INCLUDE_DIR OR NOT VEYRA_WEBGPU_LIBRARY)
  message(FATAL_ERROR
   "VEYRA_WITH_WEBGPU=ON requires wgpu-native headers and library; "
   "set VEYRA_WEBGPU_ROOT to a wgpu-native release"
  )
 endif()

 add_library(wgpu_native STATIC IMPORTED GLOBAL)
 set_target_properties(wgpu_native PROPERTIES
  IMPORTED_LOCATION "${VEYRA_WEBGPU_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${VEYRA_WEBGPU_INCLUDE_DIR}"
 )

 add_library(
  veyra_webgpu
  STATIC
  src/render/gpu/webgpu/webgpu_consumer.cpp
  src/render/gpu/webgpu/webgpu_backend.cpp
 )
 target_include_directories(
  veyra_webgpu
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_webgpu PUBLIC veyra_render wgpu_native)
 target_compile_features(veyra_webgpu PUBLIC cxx_std_23)

 if(WIN32)
  target_link_libraries(veyra_webgpu PUBLIC ws2_32 userenv dbghelp ole32 ntdll)
 elseif(APPLE)
  target_link_libraries(veyra_webgpu PUBLIC
   "-framework Metal" "-framework QuartzCore" "-framework Foundation"
   "-framework CoreFoundation" "-framework IOKit" "-framework CoreGraphics"
  )
 else()
  target_link_libraries(veyra_webgpu PUBLIC dl pthread m)
 endif()

 veyra_add_test(
  NAME webgpu_plan_behavior
  TARGET veyra_webgpu_consumer_test
  SOURCES tests/webgpu_consumer_test.cpp
  LIBS veyra_webgpu
 )
 veyra_add_test(
  NAME webgpu_device_behavior
  TARGET veyra_webgpu_device_test
  SOURCES tests/webgpu_device_test.cpp
  LIBS veyra_webgpu
  SKIP_RETURN_CODE 77
 )
endif()
