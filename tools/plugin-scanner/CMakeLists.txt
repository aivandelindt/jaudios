# Out-of-process plugin scanner (synth-and-vst3-host). Shipped next to the app binary.
juce_add_console_app(jaudios-plugin-scanner PRODUCT_NAME "jaudios-plugin-scanner")
target_sources(jaudios-plugin-scanner PRIVATE main.cpp)
target_link_libraries(jaudios-plugin-scanner PRIVATE jaudios_juce jaudios_warnings)
