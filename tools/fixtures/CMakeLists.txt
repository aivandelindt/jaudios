juce_add_console_app(fixture-render PRODUCT_NAME "fixture-render")
target_sources(fixture-render PRIVATE FixtureRender.cpp)
target_link_libraries(fixture-render PRIVATE jaudios_juce jaudios_warnings)

# `cmake --build build --target fixtures` renders any missing fixture into tests/fixtures/.
add_custom_target(fixtures
    COMMAND fixture-render ${PROJECT_SOURCE_DIR}/tests/fixtures
    DEPENDS fixture-render
    COMMENT "Rendering missing synthetic fixtures into tests/fixtures/")
