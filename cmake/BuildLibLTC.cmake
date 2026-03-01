# BuildLibLTC.cmake - Build libltc from submodule as a shared library
#
# libltc is LGPLv3 licensed. We build it as a shared library (DLL/SO)
# to comply with LGPL requirements (Section 4d1: shared library mechanism).

set(LIBLTC_DIR "${CMAKE_SOURCE_DIR}/deps/libltc")

if(NOT EXISTS "${LIBLTC_DIR}/src/ltc.h")
  message(FATAL_ERROR "libltc submodule not found at ${LIBLTC_DIR}. Run: git submodule update --init --recursive")
endif()

add_library(
  libltc SHARED
  "${LIBLTC_DIR}/src/ltc.c"
  "${LIBLTC_DIR}/src/encoder.c"
  "${LIBLTC_DIR}/src/decoder.c"
  "${LIBLTC_DIR}/src/timecode.c"
)

target_include_directories(libltc PUBLIC "${LIBLTC_DIR}/src")

# On Windows, use a .def file to export public API symbols
# (libltc has no __declspec(dllexport) macros)
if(WIN32)
  target_sources(libltc PRIVATE "${CMAKE_SOURCE_DIR}/deps/libltc.def")
endif()

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

# Set output directories so the shared lib ends up next to the plugin
set_target_properties(libltc PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
