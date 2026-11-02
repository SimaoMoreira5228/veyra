if(VEYRA_WITH_ATSPI)
 find_package(PkgConfig QUIET)
 if(PkgConfig_FOUND)
  pkg_check_modules(VEYRA_DBUS QUIET IMPORTED_TARGET dbus-1)
 endif()
 find_path(
  VEYRA_ATSPI_INCLUDE_DIR
  NAMES atspi/atspi-constants.h
  PATH_SUFFIXES at-spi-2.0
 )

 if(NOT VEYRA_DBUS_FOUND OR NOT VEYRA_ATSPI_INCLUDE_DIR)
  message(STATUS "Veyra: AT-SPI disabled (dbus-1 or atspi constants not found)")
 else()
  add_library(
  veyra_atspi STATIC
  src/platform/linux/atspi_sink.cpp
  src/platform/linux/atspi_dispatch.cpp
  src/platform/linux/atspi_accessible.cpp
  src/platform/linux/atspi_widgets.cpp
 )
  target_include_directories(
   veyra_atspi
   PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
   PRIVATE ${VEYRA_ATSPI_INCLUDE_DIR}
  )
  target_link_libraries(veyra_atspi PUBLIC veyra_platform PkgConfig::VEYRA_DBUS)
  target_compile_features(veyra_atspi PUBLIC cxx_std_23)

  add_executable(veyra_atspi_test tests/atspi_sink_test.cpp)
  target_include_directories(veyra_atspi_test PRIVATE ${VEYRA_ATSPI_INCLUDE_DIR})
  target_link_libraries(veyra_atspi_test PRIVATE veyra_atspi)
  find_program(VEYRA_DBUS_RUN_SESSION dbus-run-session)
  if(VEYRA_DBUS_RUN_SESSION)
   add_test(
    NAME atspi_accessibility_behavior
    COMMAND ${VEYRA_DBUS_RUN_SESSION} -- $<TARGET_FILE:veyra_atspi_test>
   )
   set_tests_properties(atspi_accessibility_behavior PROPERTIES SKIP_RETURN_CODE 77)
  endif()
 endif()
endif()
