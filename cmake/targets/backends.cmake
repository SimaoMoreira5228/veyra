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
 find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3)
 add_library(veyra_sdl3 STATIC src/platform/sdl3_window.cpp)
 target_include_directories(
  veyra_sdl3
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_sdl3 PUBLIC veyra SDL3::SDL3)
 target_compile_features(veyra_sdl3 PUBLIC cxx_std_23)

 veyra_add_test(NAME sdl3_window_behavior TARGET veyra_sdl3_window_test SOURCES tests/sdl3_window_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")

 target_sources(veyra_sdl3 PRIVATE src/platform/sdl3_translate.cpp)

 veyra_add_test(NAME sdl3_translate_behavior TARGET veyra_sdl3_translate_test SOURCES tests/sdl3_translate_test.cpp LIBS veyra_sdl3 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
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

 veyra_add_test(NAME glfw_window_behavior TARGET veyra_glfw_window_test SOURCES tests/glfw_window_test.cpp LIBS veyra_glfw ${veyra_glfw_target} SKIP_RETURN_CODE 77)
endif()
