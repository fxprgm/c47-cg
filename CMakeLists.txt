# The C47 add-in for the fx-CG50 (fxSDK + gint).
#
#   python tools/sync_upstream.py      # upstream C47 + patches + generated sources (once, and
#                                      # after changing UPSTREAM or patches/)
#   fxsdk build-cg                     # -> C47.g3a
#
# The C47 sources are upstream's own lists for its calculator builds (build/sources.cmake,
# written by sync_upstream.py from upstream's meson.build files).

cmake_minimum_required(VERSION 3.15)
project(C47CG VERSION 0.1.0 LANGUAGES C)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(C47_UPSTREAM ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(C47_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/build/generated)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/build/sources.cmake)
  message(FATAL_ERROR "Run python tools/sync_upstream.py first")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/build/sources.cmake)

set(SOURCES
  ${C47_CORE_SOURCES}
  ${C47_DECNUMBER_SOURCES}
  ${C47_DMCP_HAL_SOURCES}
  ${C47_GENERATED}/rasterFontsData.c
  ${C47_GENERATED}/constantPointers.c
  ${C47_GENERATED}/constantPointers2.c
  src/dmcp_lcd.c
  src/dmcp_sys.c
  src/dmcp_ff.c
  src/view.c
  src/plat_gint.c
  src/gmp/mini-gmp.c
  src/gmp/mini-gmp-extra.c
  src/compat/compat.c
)

add_executable(c47 ${SOURCES})

target_include_directories(c47 PRIVATE
  src
  src/dmcp
  src/gmp
  src/compat
  ${C47_GENERATED}
  ${C47_UPSTREAM}/src/c47
  ${C47_UPSTREAM}/dep/decNumberICU
)

# As upstream's DMCP5 (R47 hardware) build: DMCP_BUILD, NEW_HW, 32-bit, the R47 keyboard model.
# DECLITEND=0: decNumber's encodings in big-endian byte order (the SuperH is big-endian;
# sync_upstream.py swapped the generated constants to match).
target_compile_definitions(c47 PRIVATE
  DMCP_BUILD OS32BIT NEW_HW DECNUMBER_FASTMUL=1 CALCMODEL=USER_R47 DECLITEND=0
  "__weak=__attribute__((weak))" "__packed=__attribute__((__packed__))"
)
target_compile_options(c47 PRIVATE
  -Os -fsigned-char -fno-strict-aliasing -ffunction-sections -fdata-sections
  -Wno-unused-parameter
  -include ${CMAKE_CURRENT_SOURCE_DIR}/src/compat/compat.h
)
target_link_options(c47 PRIVATE -Wl,--gc-sections)
target_link_libraries(c47 Gint::Gint -lm)

if("${FXSDK_PLATFORM_LONG}" STREQUAL fxCG50)
  generate_g3a(TARGET c47 OUTPUT "C47.g3a"
    NAME "C47" ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
endif()
