cmake_minimum_required(VERSION 3.16)
project(sustainhold CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- proxy ---
add_library(proxy SHARED src/proxy.cpp)
target_include_directories(proxy PRIVATE sdk)
set_target_properties(proxy PROPERTIES PREFIX "")   # proxy.dll / proxy.dylib

if(APPLE)
  set_target_properties(proxy PROPERTIES
    MACOSX_RPATH ON
    LINK_FLAGS "-Wl,-undefined,dynamic_lookup")
  # assemble the .vst bundle next to the raw dylib (host_test uses the raw one)
  set(BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/TAL-U-NO-LX-V2.vst")
  add_custom_command(TARGET proxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:proxy>" "${BUNDLE}/Contents/MacOS/TAL-U-NO-LX-V2"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist" "${BUNDLE}/Contents/Info.plist"
    COMMAND codesign --force --timestamp=none --sign - "${BUNDLE}"
    COMMENT "assembled + ad-hoc signed TAL-U-NO-LX-V2.vst")
endif()

# ------------------------------------------------------------ test target --
add_library(test_target SHARED src/test_target.cpp)
target_include_directories(test_target PRIVATE sdk)
set_target_properties(test_target PROPERTIES PREFIX "")

# ---------------------------------------------------------------- hosts ---
add_executable(host_test src/host_test.cpp)
target_include_directories(host_test PRIVATE sdk)

add_executable(smoke_tal src/smoke_tal.cpp)
target_include_directories(smoke_tal PRIVATE sdk)

add_executable(test_switch src/test_switch.cpp)
target_include_directories(test_switch PRIVATE sdk)

# Windows-only installed-plugin probes
if(WIN32)
  foreach(p poly_probe poly_probe2 verify_installed direct_test)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${p}.cpp")
      add_executable(${p} src/${p}.cpp)
      target_include_directories(${p} PRIVATE sdk)
    endif()
  endforeach()
endif()

enable_testing()
add_test(NAME host_test COMMAND host_test)
set_tests_properties(host_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
