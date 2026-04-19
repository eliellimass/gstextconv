cmake_minimum_required(VERSION 3.20)

project(gstextconv
    VERSION 1.0.0
    DESCRIPTION "Texture converter for Giants Engine / Farming Simulator (GS2D/.ast)"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GSTEXTCONV_BUILD_CLI    "Build the gstextconv CLI executable" ON)
option(GSTEXTCONV_BUILD_PYTHON "Build the Python extension module"   OFF)
option(GSTEXTCONV_BUILD_TESTS  "Build native C++ tests"              OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
endif()

# ------------------------------------------------------------------
# Third-party: miniz (zlib-compatible) static
# ------------------------------------------------------------------
add_library(gstextconv_miniz STATIC third_party/miniz/miniz.c)
target_include_directories(gstextconv_miniz PUBLIC third_party/miniz)
set_target_properties(gstextconv_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------
# Third-party: astcenc embedded (auto-detects SIMD from compiler flags)
# ------------------------------------------------------------------
file(GLOB GSTEXTCONV_ASTCENC_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/astcenc/astcenc_*.cpp")
add_library(gstextconv_astcenc STATIC ${GSTEXTCONV_ASTCENC_SRC})
target_include_directories(gstextconv_astcenc PUBLIC third_party/astcenc)
set_target_properties(gstextconv_astcenc PROPERTIES POSITION_INDEPENDENT_CODE ON)

# astcenc prefers a moderate ISA baseline; we rely on the compiler defaults
# plus per-arch tuning below. The header auto-selects SSE/AVX/NEON using
# compiler predefines, so the build works out-of-the-box for x86_64 and
# aarch64 targets with a modern toolchain.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)")
    if(MSVC)
        target_compile_options(gstextconv_astcenc PRIVATE /arch:AVX2)
    else()
        target_compile_options(gstextconv_astcenc PRIVATE -mavx2 -mf16c -mpopcnt)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64|ARM64)")
    if(NOT MSVC)
        target_compile_options(gstextconv_astcenc PRIVATE -march=armv8-a+crc)
    endif()
endif()

# Prune the ISA baseline when the target is 32-bit x86 (build still works).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
    target_compile_definitions(gstextconv_astcenc PRIVATE
        ASTCENC_SSE=20 ASTCENC_AVX=0 ASTCENC_POPCNT=0 ASTCENC_F16C=0 ASTCENC_NEON=0)
endif()

# ------------------------------------------------------------------
# Core static library
# ------------------------------------------------------------------
add_library(gstextconv_core STATIC
    src/container.cpp
    src/codec.cpp
    src/astc_bridge.cpp
    src/image_io.cpp
    src/color_format.cpp
    src/mipgen.cpp
    src/flip.cpp
    src/api.cpp)
target_include_directories(gstextconv_core
    PUBLIC  include
    PRIVATE third_party/stb)
target_link_libraries(gstextconv_core
    PUBLIC  gstextconv_astcenc
    PRIVATE gstextconv_miniz)

# ------------------------------------------------------------------
# CLI executable — static linkage (no shared library dependency)
# ------------------------------------------------------------------
if(GSTEXTCONV_BUILD_CLI)
    add_executable(gstextconv cli/main.cpp)
    target_link_libraries(gstextconv PRIVATE gstextconv_core)
    set_target_properties(gstextconv PROPERTIES OUTPUT_NAME gstextconv)

    # Link statically: no libc++/libstdc++/libgcc runtime dependency when
    # building for portable distribution.
    if(MSVC)
        set_property(TARGET gstextconv PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET gstextconv_core PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET gstextconv_astcenc PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET gstextconv_miniz PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded")
    elseif(MINGW OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND WIN32))
        target_link_options(gstextconv PRIVATE
            -static -static-libgcc -static-libstdc++)
    elseif(ANDROID)
        target_link_options(gstextconv PRIVATE -static-libstdc++)
        # libc on Android is always dynamic; we still ship an otherwise
        # self-contained binary by statically linking libc++.
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(gstextconv PRIVATE -static-libgcc -static-libstdc++)
    endif()

    # Install the CLI when building from source
    install(TARGETS gstextconv RUNTIME DESTINATION bin)
endif()

# ------------------------------------------------------------------
# Python extension (optional, built by scikit-build-core)
# ------------------------------------------------------------------
if(GSTEXTCONV_BUILD_PYTHON)
    if(ANDROID)
        # On Android the NDK linker resolves all symbols at link time, so we
        # need the full Python dev component (libpython3.x.so) available.
        find_package(Python 3.13 REQUIRED COMPONENTS Interpreter Development)
    else()
        find_package(Python 3.13 REQUIRED COMPONENTS Interpreter Development.Module)
    endif()

    # pybind11 is either vendored by scikit-build-core via the
    # `pybind11[global]` build requirement or discovered on the host.
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(_gstextconv_native MODULE
        python/_bindings.cpp)
    target_link_libraries(_gstextconv_native PRIVATE gstextconv_core)
    if(ANDROID)
        # Link libpython3.x.so so all Py* symbols resolve at build time
        # (Android's dynamic linker is strict about undefined symbols).
        target_link_libraries(_gstextconv_native PRIVATE Python::Python)
    endif()

    install(TARGETS _gstextconv_native
        LIBRARY DESTINATION gstextconv
        RUNTIME DESTINATION gstextconv)
    install(DIRECTORY python/gstextconv/
        DESTINATION gstextconv
        FILES_MATCHING PATTERN "*.py")
endif()

if(GSTEXTCONV_BUILD_TESTS)
    enable_testing()
    add_executable(gstextconv_tests tests/cpp/test_main.cpp)
    target_link_libraries(gstextconv_tests PRIVATE gstextconv_core)
    add_test(NAME gstextconv_tests COMMAND gstextconv_tests)
endif()
