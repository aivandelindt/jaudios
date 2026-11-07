cmake_minimum_required(VERSION 3.25)

project(JAudios VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(JAUDIOS_BUILD_APP     "Build the JUCE GUI standalone app"                         ON)
option(JAUDIOS_BUILD_TESTS   "Build Catch2 unit tests"                                   ON)
option(JAUDIOS_SANITIZE      "Enable -fsanitize=address,undefined in Debug builds"       ON)
option(JAUDIOS_WITH_ONNXRUNTIME "Build the Phase B ONNX key detector (ONNX Runtime)"           ON)
option(JAUDIOS_RT_CHECK      "Build the realtime allocation/lock trap (tools/rt_check.sh)" OFF)

# Debug sanitizers (scaffold step 6). Incompatible with the RT allocation trap, which
# interposes malloc, so rt_check builds turn them off.
if(JAUDIOS_SANITIZE AND NOT JAUDIOS_RT_CHECK AND NOT MSVC)
    add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>
                        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
    add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

include(cmake/Dependencies.cmake)
include(cmake/JuceShared.cmake)
if(JAUDIOS_WITH_ONNXRUNTIME)
    include(cmake/OnnxRuntime.cmake)
endif()

add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(tools/cli)
add_subdirectory(tools/fixtures)
add_subdirectory(tools/plugin-scanner)

if(JAUDIOS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
