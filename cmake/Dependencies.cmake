# Third-party dependencies. Every entry here must be listed in THIRD_PARTY.md and be in the
# AGENTS.md stack table. Ask before adding anything new.
#
# To build offline / from a local checkout, pass e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE -DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(JAUDIOS_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# sfizz (SFZ synth, BSD-2; stack table: built-in sounds). Static, no apps/plugins/tests; its bundled
# deps (abseil, dr_libs, WavPack, simde, ghc filesystem, pugixml, kiss_fft, cpuid, hiir, ...) are
# listed in THIRD_PARTY.md. Pinned by commit (tag 1.2.3). Approved 2026-11-07.
set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(SFIZZ_USE_SNDFILE OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE) # sfizz's own LTO switch; keeps its build time down
# sfizz must build as C++17: GCC rejects its hex float literals in its default C++14, and in C++20
# its u8"" literals become char8_t. It runs set(CMAKE_CXX_STANDARD 14 CACHE ...) under old CMP0126,
# which drops the normal variable in its scope, so set 17 both in the cache (first configure) and as
# the normal variable around its subdirectory; our own targets keep C++20 (restored below).
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard for third-party subprojects that read the cache" FORCE)
set(_jaudios_cxx_standard ${CMAKE_CXX_STANDARD})
FetchContent_Declare(sfizz
    GIT_REPOSITORY https://github.com/sfztools/sfizz.git
    GIT_TAG        4e70dc0bef53b41f2853ed46e26f5911114c92d0 # 1.2.3
    GIT_SUBMODULES_RECURSE TRUE
    GIT_SHALLOW    FALSE
    SYSTEM)
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(sfizz)
set(CMAKE_CXX_STANDARD ${_jaudios_cxx_standard})
