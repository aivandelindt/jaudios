include(Catch)

# Shared helpers (synthetic signals, temp files) for every test binary.
add_library(test_support INTERFACE)
target_include_directories(test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_support INTERFACE
    JAUDIOS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(unit/sources.cmake)

add_executable(jaudios_tests TestMain.cpp ${JAUDIOS_TEST_SOURCES})
target_link_libraries(jaudios_tests PRIVATE Catch2::Catch2 app_core test_support jaudios_warnings)
catch_discover_tests(jaudios_tests DISCOVERY_MODE PRE_TEST)

# Same tests, linked with the realtime trap (tools/rt_check.sh). Tests tagged [rt] assert zero
# allocations on the audio path; elsewhere they SKIP.
if(JAUDIOS_RT_CHECK)
    add_executable(jaudios_rt_tests TestMain.cpp ${JAUDIOS_TEST_SOURCES} $<TARGET_OBJECTS:app_rt_trap_hooks>)
    target_link_libraries(jaudios_rt_tests PRIVATE Catch2::Catch2 app_core test_support ${CMAKE_DL_LIBS})
    if(NOT APPLE AND NOT MSVC)
        target_link_options(jaudios_rt_tests PRIVATE -rdynamic) # readable JAUDIOS_RT_BACKTRACE output
    endif()
endif()
