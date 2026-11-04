# CLI harness (juce-standalone-scaffold step 5): every feature works headless before UI.
juce_add_console_app(app-cli PRODUCT_NAME "app-cli")
target_sources(app-cli PRIVATE main.cpp)
target_link_libraries(app-cli PRIVATE app_core jaudios_warnings)

# Instrumented build used by tools/rt_check.sh.
if(JAUDIOS_RT_CHECK)
    juce_add_console_app(app-cli-rt PRODUCT_NAME "app-cli-rt")
    target_sources(app-cli-rt PRIVATE main.cpp $<TARGET_OBJECTS:app_rt_trap_hooks>)
    target_link_libraries(app-cli-rt PRIVATE app_core jaudios_warnings ${CMAKE_DL_LIBS})
endif()
