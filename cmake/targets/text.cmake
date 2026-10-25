if(VEYRA_WITH_TEXT)
 find_package(harfbuzz CONFIG REQUIRED)
 add_library(veyra_text STATIC
  src/text/glyph_run.cpp
  src/text/shaped_run_cache.cpp
  src/text/shaper.cpp
 )
 target_include_directories(veyra_text PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_text PUBLIC veyra harfbuzz::harfbuzz)
 target_compile_features(veyra_text PUBLIC cxx_std_23)

 veyra_add_test(NAME text_shaper_behavior TARGET veyra_text_shaper_test SOURCES tests/text_shaper_test.cpp LIBS veyra_text)
 veyra_add_test(NAME shaped_run_cache_behavior TARGET veyra_shaped_run_cache_test SOURCES tests/shaped_run_cache_test.cpp LIBS veyra_text)
 veyra_add_test(NAME line_measure_cache_behavior TARGET veyra_line_measure_cache_test SOURCES tests/line_measure_cache_test.cpp LIBS veyra_text)

 if(VEYRA_BUILD_BENCH)
  veyra_add_test(TARGET veyra_text_shaper_bench SOURCES bench/text_shaper.cpp LIBS veyra_text BENCH_DEFINES NO_TEST)
  veyra_add_test(TARGET veyra_shaped_run_cache_bench SOURCES bench/shaped_run_cache.cpp LIBS veyra_text BENCH_DEFINES NO_TEST)
  veyra_add_test(NAME shaped_run_cache_benchmark_smoke TARGET veyra_shaped_run_cache_bench NO_BUILD)
 endif()
endif()

if(VEYRA_WITH_UNICODE)
 find_package(ICU 74 REQUIRED COMPONENTS i18n uc)
 add_library(veyra_unicode STATIC
  src/text/bidi_resolver_icu.cpp
  src/text/grapheme_icu.cpp
  src/text/line_breaker_icu.cpp
  src/text/utf8.cpp
 )
 target_include_directories(veyra_unicode PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
 target_link_libraries(veyra_unicode PUBLIC veyra ICU::i18n ICU::uc)
 target_compile_features(veyra_unicode PUBLIC cxx_std_23)

 veyra_add_test(NAME grapheme_navigation_behavior TARGET veyra_grapheme_navigation_test SOURCES tests/grapheme_navigation_test.cpp LIBS veyra_unicode)
 veyra_add_test(NAME text_line_breaker_behavior TARGET veyra_line_breaker_test SOURCES tests/line_breaker_test.cpp LIBS veyra_unicode)
 veyra_add_test(NAME text_bidi_resolver_behavior TARGET veyra_bidi_resolver_test SOURCES tests/bidi_resolver_test.cpp LIBS veyra_unicode)

 if(VEYRA_WITH_TEXT)
  add_library(veyra_font_fallback STATIC src/text/font_fallback_icu.cpp)
  target_include_directories(
   veyra_font_fallback
   PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_link_libraries(veyra_font_fallback PUBLIC veyra_text veyra_unicode)
  target_compile_features(veyra_font_fallback PUBLIC cxx_std_23)

  veyra_add_test(NAME text_font_fallback_behavior TARGET veyra_font_fallback_test SOURCES tests/font_fallback_test.cpp LIBS veyra_font_fallback)
  veyra_add_test(NAME text_shaping_pipeline_behavior TARGET veyra_shaping_pipeline_test SOURCES tests/shaping_pipeline_test.cpp LIBS veyra_font_fallback)

  if(VEYRA_BUILD_BENCH)
   veyra_add_test(NAME text_font_fallback_benchmark_smoke TARGET veyra_font_fallback_bench SOURCES bench/font_fallback.cpp LIBS veyra_font_fallback BENCH_DEFINES)
  endif()
 endif()

 if(VEYRA_BUILD_BENCH)
  veyra_add_test(NAME text_bidi_resolver_benchmark_smoke TARGET veyra_bidi_resolver_bench SOURCES bench/bidi_resolver.cpp LIBS veyra_unicode BENCH_DEFINES)
  veyra_add_test(NAME text_line_breaker_benchmark_smoke TARGET veyra_line_breaker_bench SOURCES bench/line_breaker.cpp LIBS veyra_unicode BENCH_DEFINES)
 endif()

 if(VEYRA_BUILD_BENCH)
  veyra_add_test(NAME grapheme_navigation_benchmark_smoke TARGET veyra_grapheme_navigation_bench SOURCES bench/grapheme_navigation.cpp LIBS veyra_unicode BENCH_DEFINES)
 endif()
endif()

if(VEYRA_WITH_FREETYPE)
 find_package(Freetype 2.14.3 REQUIRED)
 add_library(veyra_freetype STATIC src/text/freetype_rasterizer.cpp)
 target_include_directories(
  veyra_freetype
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 )
 target_link_libraries(veyra_freetype PUBLIC veyra Freetype::Freetype)
 target_compile_features(veyra_freetype PUBLIC cxx_std_23)

 veyra_add_test(NAME freetype_rasterizer_behavior TARGET veyra_freetype_rasterizer_test SOURCES tests/freetype_rasterizer_test.cpp LIBS veyra_freetype)

 if(VEYRA_BUILD_BENCH)
  veyra_add_test(NAME freetype_color_bitmap_benchmark_smoke TARGET veyra_freetype_color_bitmap_bench SOURCES bench/freetype_color_bitmap.cpp LIBS veyra_freetype BENCH_DEFINES)
  veyra_add_test(TARGET veyra_freetype_rasterizer_bench SOURCES bench/freetype_rasterizer.cpp LIBS veyra_freetype BENCH_DEFINES NO_TEST)
 endif()
endif()
