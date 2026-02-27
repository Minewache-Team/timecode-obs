# BuildLibLTC.cmake - Build libltc from submodule as a static library

set(LIBLTC_DIR "${CMAKE_SOURCE_DIR}/deps/libltc")

if(NOT EXISTS "${LIBLTC_DIR}/src/ltc.h")
  message(FATAL_ERROR "libltc submodule not found at ${LIBLTC_DIR}. Run: git submodule update --init --recursive")
endif()

add_library(
  libltc STATIC
  "${LIBLTC_DIR}/src/ltc.c"
  "${LIBLTC_DIR}/src/encoder.c"
  "${LIBLTC_DIR}/src/decoder.c"
  "${LIBLTC_DIR}/src/timecode.c"
)

target_include_directories(libltc PUBLIC "${LIBLTC_DIR}/src")

# Suppress warnings in third-party code
if(MSVC)
  target_compile_options(libltc PRIVATE /W0)
else()
  target_compile_options(libltc PRIVATE -w)
endif()

# libltc needs math library on Linux
if(UNIX)
  target_link_libraries(libltc PRIVATE m)
endif()

set_target_properties(libltc PROPERTIES POSITION_INDEPENDENT_CODE ON)
