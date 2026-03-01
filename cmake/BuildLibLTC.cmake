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

# macOS: set install name so the dylib can be found via @rpath
if(APPLE)
  set_target_properties(libltc PROPERTIES
    INSTALL_NAME_DIR "@rpath"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
endif()

# Install libltc shared library alongside the plugin for each platform.
# This ensures cmake --install populates the release staging directory correctly.
if(WIN32)
  install(TARGETS libltc RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
elseif(APPLE)
  install(TARGETS libltc LIBRARY DESTINATION "${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
else()
  include(GNUInstallDirs)
  install(TARGETS libltc LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins)
endif()

# Install LGPL license for libltc (required by LGPLv3 Section 4a/4b)
set(LIBLTC_LICENSE "${LIBLTC_DIR}/COPYING")
if(EXISTS "${LIBLTC_LICENSE}")
  if(WIN32)
    install(FILES "${LIBLTC_LICENSE}" DESTINATION "${CMAKE_PROJECT_NAME}/licenses/libltc" RENAME "COPYING.LGPLv3")
  elseif(APPLE)
    install(FILES "${LIBLTC_LICENSE}" DESTINATION "${CMAKE_PROJECT_NAME}.plugin/Contents/Resources/licenses/libltc" RENAME "COPYING.LGPLv3")
  else()
    include(GNUInstallDirs)
    install(FILES "${LIBLTC_LICENSE}" DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${CMAKE_PROJECT_NAME}/licenses/libltc RENAME "COPYING.LGPLv3")
  endif()
endif()
