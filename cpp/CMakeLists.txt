cmake_minimum_required(VERSION 3.21)
project(PicViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# LibRaw and libheif ship pkg-config files in MSYS2 but no CMake config, so
# they are found that way rather than with find_package.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)
pkg_check_modules(LIBHEIF REQUIRED libheif)

add_executable(picviewer WIN32
    src/main.cpp
    src/sniff.cpp
    src/color.cpp
    src/cache.cpp
    src/loader.cpp
    src/viewer.cpp
    src/window.cpp
    src/applog.cpp
    src/decoders/registry.cpp
    src/decoders/qt_decoder.cpp
    src/decoders/heif_decoder.cpp
    src/decoders/raw_decoder.cpp
    resources.qrc
)

target_include_directories(picviewer PRIVATE
    src
    ${LIBRAW_INCLUDE_DIRS}
    ${LIBHEIF_INCLUDE_DIRS}
)

target_link_libraries(picviewer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${LIBRAW_LIBRARIES}
    ${LIBHEIF_LIBRARIES}
)

target_link_directories(picviewer PRIVATE
    ${LIBRAW_LIBRARY_DIRS}
    ${LIBHEIF_LIBRARY_DIRS}
)

target_compile_options(picviewer PRIVATE
    -Wall -Wextra -Wpedantic
    ${LIBRAW_CFLAGS_OTHER}
    ${LIBHEIF_CFLAGS_OTHER}
)

# ---------------------------------------------------------------------------
# Tests. The same split as the Python version: assertion-based unit tests over
# the pure logic, and end-to-end suites that drive the real thing.
# ---------------------------------------------------------------------------

enable_testing()

add_library(picviewer_core STATIC
    src/sniff.cpp
    src/color.cpp
    src/cache.cpp
    src/decoders/registry.cpp
    src/decoders/qt_decoder.cpp
    src/decoders/heif_decoder.cpp
    src/decoders/raw_decoder.cpp
)
target_include_directories(picviewer_core PUBLIC
    src ${LIBRAW_INCLUDE_DIRS} ${LIBHEIF_INCLUDE_DIRS})
target_link_libraries(picviewer_core PUBLIC
    Qt6::Core Qt6::Gui ${LIBRAW_LIBRARIES} ${LIBHEIF_LIBRARIES})
target_link_directories(picviewer_core PUBLIC
    ${LIBRAW_LIBRARY_DIRS} ${LIBHEIF_LIBRARY_DIRS})

add_subdirectory(tests)
