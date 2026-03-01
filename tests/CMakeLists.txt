include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Include source directory for headers
include_directories("${CMAKE_SOURCE_DIR}/src")

# --- test-timecode ---
add_executable(test-timecode test-timecode.cpp "${CMAKE_SOURCE_DIR}/src/timecode.c")
target_link_libraries(test-timecode PRIVATE GTest::gtest_main)
add_test(NAME TimecodeTests COMMAND test-timecode)

# --- test-ntp-offset ---
add_executable(test-ntp-offset test-ntp-offset.cpp "${CMAKE_SOURCE_DIR}/src/ntp-client.c")
target_link_libraries(test-ntp-offset PRIVATE GTest::gtest_main)
add_test(NAME NTPOffsetTests COMMAND test-ntp-offset)

# --- test-ltc-roundtrip ---
add_executable(test-ltc-roundtrip test-ltc-roundtrip.cpp "${CMAKE_SOURCE_DIR}/src/ltc-encoder-wrapper.c")
target_link_libraries(test-ltc-roundtrip PRIVATE GTest::gtest_main libltc)
add_test(NAME LTCRoundtripTests COMMAND test-ltc-roundtrip)

# Copy libltc shared library next to test executable so it can be found at runtime
if(WIN32)
  add_custom_command(TARGET test-ltc-roundtrip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libltc> $<TARGET_FILE_DIR:test-ltc-roundtrip>
    COMMENT "Copying libltc.dll to test directory"
  )
endif()

# --- test-http-time ---
find_package(CURL REQUIRED)
add_executable(test-http-time test-http-time.cpp "${CMAKE_SOURCE_DIR}/src/http-time-client.c")
target_link_libraries(test-http-time PRIVATE GTest::gtest_main CURL::libcurl)
if(WIN32)
  target_link_libraries(test-http-time PRIVATE ws2_32)
endif()
add_test(NAME HTTPTimeTests COMMAND test-http-time)
