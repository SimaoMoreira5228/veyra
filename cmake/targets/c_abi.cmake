if(VEYRA_BUILD_C_ABI)
 enable_language(C)

 add_library(veyra_c STATIC src/c/veyra_c.cpp)
 target_include_directories(
  veyra_c
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_compile_definitions(veyra_c PRIVATE VEYRA_C_BUILD)
 target_link_libraries(veyra_c PUBLIC veyra_style)
 target_compile_features(veyra_c PUBLIC cxx_std_23)
 set_target_properties(
  veyra_c veyra_style veyra_foundation
  PROPERTIES POSITION_INDEPENDENT_CODE ON
 )

 veyra_add_test(
  NAME c_abi_behavior
  TARGET veyra_c_abi_test
  SOURCES tests/c_abi_test.c
  LIBS veyra_c
 )
 set_target_properties(veyra_c_abi_test PROPERTIES LINKER_LANGUAGE CXX)
endif()
