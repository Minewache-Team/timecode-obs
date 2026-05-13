cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" OFF)
option(BUILD_TESTS "Build unit tests" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

# Build libltc from submodule
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildLibLTC.cmake")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libltc)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
endif()

# libcurl for HTTP time fallback
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Windows: link Winsock2 for NTP client
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.c
          src/ltc-source.c
          src/ltc-source.h
          src/ntp-client.c
          src/ntp-client.h
          src/timecode.c
          src/timecode.h
          src/ltc-encoder-wrapper.c
          src/ltc-encoder-wrapper.h
          src/auto-setup.c
          src/auto-setup.h
          src/http-time-client.c
          src/http-time-client.h
          src/metadata-writer.c
          src/metadata-writer.h
          src/mw-recording.c
          src/mw-recording.h
          src/mw-recording-helpers.c
          src/mw-recording-helpers.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# macOS: add rpath so plugin can find libltc.dylib inside the bundle's Frameworks/
if(APPLE)
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "@loader_path/../Frameworks"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
