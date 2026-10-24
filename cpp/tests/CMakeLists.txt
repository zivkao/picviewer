find_package(Qt6 REQUIRED COMPONENTS Test)

# Both binaries need to know where the sample files are, and the functional
# suites drive the real MainWindow, so both link the same library the
# application does.
set(PICVIEWER_TEST_DEFINES
    PICVIEWER_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../samples"
    # Kept apart from samples/ so the folder-walking suites do not decode a
    # 309 MiB image on every pass.
    PICVIEWER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_executable(unit_tests unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE picviewer_core Qt6::Test)
target_compile_definitions(unit_tests PRIVATE ${PICVIEWER_TEST_DEFINES})

add_executable(functional_tests functional_tests.cpp)
target_link_libraries(functional_tests PRIVATE picviewer_core Qt6::Test)
target_compile_definitions(functional_tests PRIVATE ${PICVIEWER_TEST_DEFINES})

add_test(NAME unit COMMAND unit_tests)
add_test(NAME functional COMMAND functional_tests)

# Offscreen so neither needs a display, exactly as the Python suites do.
set_tests_properties(unit functional
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The functional suites open and decode real files, including a 19 MB RAW when
# one is present, so they are allowed longer than the default 1500 seconds is
# generous for but the unit tests would never need.
set_tests_properties(functional PROPERTIES TIMEOUT 600)
