find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(unit_tests unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE picviewer_core Qt6::Test)
target_compile_definitions(unit_tests PRIVATE
    PICVIEWER_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../samples")

add_test(NAME unit COMMAND unit_tests)

# The tests run offscreen so they need no display, exactly as the Python
# suites do with QT_QPA_PLATFORM.
set_tests_properties(unit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
