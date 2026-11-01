include_guard(GLOBAL)

if(POLICY CMP0169)
 cmake_policy(SET CMP0169 OLD)
endif()

option(VEYRA_HERMETIC "Build pinned third-party dependencies from source" ON)
set(VEYRA_DEPS_CACHE "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Shared FetchContent cache directory")

if(VEYRA_HERMETIC)
 include(FetchContent)
 set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
 if(VEYRA_DEPS_CACHE)
  set(FETCHCONTENT_BASE_DIR "${VEYRA_DEPS_CACHE}" CACHE PATH "" FORCE)
 endif()

 set(VEYRA_SDL3_URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz")
 set(VEYRA_SDL3_SHA256 "c4b08b950bd29d83caae0ab8d884298d27046a9d30700fb42841c50992665c90")
 set(VEYRA_HARFBUZZ_URL "https://github.com/harfbuzz/harfbuzz/archive/refs/tags/14.1.0.tar.gz")
 set(VEYRA_HARFBUZZ_SHA256 "9deffe9dded3c367c7d08265fd0a64193fad505a362a4889d898568c1cd46812")
 set(VEYRA_FREETYPE_URL "https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz")
 set(VEYRA_FREETYPE_SHA256 "36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f")
 set(VEYRA_GLFW_URL "https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz")
 set(VEYRA_GLFW_SHA256 "c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01")
 set(VEYRA_ICU_URL "https://github.com/unicode-org/icu/releases/download/release-77-1/icu4c-77_1-src.tgz")
 set(VEYRA_ICU_SHA256 "588e431f77327c39031ffbb8843c0e3bc122c211374485fa87dc5f3faff24061")
endif()

function(veyra_fetch_dependency name url hash)
 FetchContent_Declare(
  ${name}
  URL "${url}"
  URL_HASH "SHA256=${hash}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
 )
 FetchContent_MakeAvailable(${name})
endfunction()

function(veyra_dependency_sdl3)
 if(TARGET SDL3::SDL3)
  return()
 endif()
 if(NOT VEYRA_HERMETIC)
  find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3)
  return()
 endif()
 set(SDL_SHARED OFF CACHE BOOL "" FORCE)
 set(SDL_STATIC ON CACHE BOOL "" FORCE)
 set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
 set(SDL_TESTS OFF CACHE BOOL "" FORCE)
 set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
 set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
 set(SDL_DBUS OFF CACHE BOOL "" FORCE)
 set(SDL_IBUS OFF CACHE BOOL "" FORCE)
 set(SDL_LIBURING OFF CACHE BOOL "" FORCE)
 set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
 set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
 if(VEYRA_GPU_SDL)
  set(SDL_VULKAN ON CACHE BOOL "" FORCE)
  set(SDL_RENDER_GPU ON CACHE BOOL "" FORCE)
 else()
  set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
 endif()
 set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
 set(SDL_PULSEAUDIO OFF CACHE BOOL "" FORCE)
 set(SDL_ALSA OFF CACHE BOOL "" FORCE)
 set(SDL_JACK OFF CACHE BOOL "" FORCE)
 set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
 set(SDL_DISKAUDIO OFF CACHE BOOL "" FORCE)
 set(SDL_DUMMYAUDIO ON CACHE BOOL "" FORCE)
 set(SDL_KMSDRM OFF CACHE BOOL "" FORCE)
 set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
 set(SDL_VIRTUAL_JOYSTICK ON CACHE BOOL "" FORCE)
 if(UNIX AND NOT APPLE)
  set(SDL_X11 ON CACHE BOOL "" FORCE)
  set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
  set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
 endif()
 veyra_fetch_dependency(SDL3 "${VEYRA_SDL3_URL}" "${VEYRA_SDL3_SHA256}")
 if(NOT TARGET SDL3::SDL3)
  message(FATAL_ERROR "VEYRA_HERMETIC: SDL3 did not define SDL3::SDL3")
 endif()
endfunction()

function(veyra_dependency_harfbuzz)
 if(TARGET harfbuzz::harfbuzz)
  return()
 endif()
 if(NOT VEYRA_HERMETIC)
  find_package(harfbuzz CONFIG REQUIRED)
  return()
 endif()
 set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
 set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
 set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
 set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
 set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
 set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
 set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)
 veyra_fetch_dependency(harfbuzz "${VEYRA_HARFBUZZ_URL}" "${VEYRA_HARFBUZZ_SHA256}")
 if(TARGET harfbuzz AND NOT TARGET harfbuzz::harfbuzz)
  add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
 endif()
 if(NOT TARGET harfbuzz::harfbuzz)
  message(FATAL_ERROR "VEYRA_HERMETIC: HarfBuzz did not define harfbuzz::harfbuzz")
 endif()
endfunction()

function(veyra_dependency_freetype)
 if(TARGET Freetype::Freetype)
  return()
 endif()
 if(NOT VEYRA_HERMETIC)
  find_package(Freetype 2.14.3 REQUIRED)
  return()
 endif()
 set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
 set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
 set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
 set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
 set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
 veyra_fetch_dependency(freetype "${VEYRA_FREETYPE_URL}" "${VEYRA_FREETYPE_SHA256}")
 if(TARGET freetype AND NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
 endif()
 if(NOT TARGET Freetype::Freetype)
  message(FATAL_ERROR "VEYRA_HERMETIC: FreeType did not define Freetype::Freetype")
 endif()
endfunction()

function(veyra_dependency_glfw)
 if(TARGET glfw OR TARGET glfw3::glfw)
  return()
 endif()
 if(NOT VEYRA_HERMETIC)
  find_package(glfw3 3.4 CONFIG REQUIRED)
  return()
 endif()
 set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
 set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
 set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
 set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
 set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
 veyra_fetch_dependency(glfw "${VEYRA_GLFW_URL}" "${VEYRA_GLFW_SHA256}")
 if(NOT TARGET glfw AND NOT TARGET glfw3::glfw)
  message(FATAL_ERROR "VEYRA_HERMETIC: GLFW did not define a supported target")
 endif()
endfunction()

if(VEYRA_HERMETIC)
 message(STATUS "Veyra: hermetic dependencies enabled")
 message(STATUS "Veyra: dependency cache: ${VEYRA_DEPS_CACHE}")
 message(STATUS "Veyra: pinned: SDL3 3.4.18, HarfBuzz 14.1.0, FreeType 2.14.3, GLFW 3.4, WebP 1.6.0, libavif 1.4.2")
 message(STATUS "Veyra: exceptions: OS window-system/runtime libraries")
endif()

function(veyra_dependency_icu)
 if(TARGET ICU::i18n AND TARGET ICU::uc)
  return()
 endif()
 if(NOT VEYRA_HERMETIC OR NOT UNIX)
  find_package(ICU 74 REQUIRED COMPONENTS i18n uc OPTIONAL_COMPONENTS data)
  return()
 endif()
 FetchContent_Declare(
  icu
  URL "${VEYRA_ICU_URL}"
  URL_HASH "SHA256=${VEYRA_ICU_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
 )
 FetchContent_Populate(icu)
 set(veyra_icu_prefix "${CMAKE_BINARY_DIR}/icu-install")
 set(veyra_icu_source "${icu_SOURCE_DIR}/source")
 file(MAKE_DIRECTORY "${veyra_icu_prefix}/include")
 file(MAKE_DIRECTORY "${veyra_icu_prefix}/lib")
 set(veyra_icu_jobs "${CMAKE_BUILD_PARALLEL_LEVEL}")
 if(veyra_icu_jobs STREQUAL "")
  set(veyra_icu_jobs 4)
 endif()
 include(ExternalProject)
 ExternalProject_Add(
  veyra_icu_build
  SOURCE_DIR "${veyra_icu_source}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/icu-build"
  CONFIGURE_COMMAND
   "${veyra_icu_source}/configure"
   "--prefix=${veyra_icu_prefix}"
   --disable-shared --enable-static --disable-samples --disable-tests --disable-extras
   --with-data-packaging=static
  BUILD_COMMAND ${CMAKE_MAKE_PROGRAM} -j${veyra_icu_jobs}
  INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install
  BUILD_BYPRODUCTS
   "${veyra_icu_prefix}/lib/libicuuc.a"
   "${veyra_icu_prefix}/lib/libicui18n.a"
   "${veyra_icu_prefix}/lib/libicudata.a"
  UPDATE_COMMAND ""
  DOWNLOAD_COMMAND ""
 )
 add_library(ICU::data STATIC IMPORTED GLOBAL)
 set_target_properties(ICU::data PROPERTIES IMPORTED_LOCATION "${veyra_icu_prefix}/lib/libicudata.a")
 add_library(ICU::uc STATIC IMPORTED GLOBAL)
 set_target_properties(ICU::uc PROPERTIES
  IMPORTED_LOCATION "${veyra_icu_prefix}/lib/libicuuc.a"
  INTERFACE_INCLUDE_DIRECTORIES "${veyra_icu_prefix}/include"
  INTERFACE_LINK_LIBRARIES ICU::data
 )
 add_library(ICU::i18n STATIC IMPORTED GLOBAL)
 set_target_properties(ICU::i18n PROPERTIES
  IMPORTED_LOCATION "${veyra_icu_prefix}/lib/libicui18n.a"
  INTERFACE_INCLUDE_DIRECTORIES "${veyra_icu_prefix}/include"
  INTERFACE_LINK_LIBRARIES "ICU::uc;ICU::data"
 )
endfunction()
