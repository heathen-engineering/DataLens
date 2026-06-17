cmake_minimum_required(VERSION 3.16)
project(datalens VERSION 0.1.0 LANGUAGES CXX)

# ── Options ──────────────────────────────────────────────────────────────────
option(DATALENS_BUILD_TESTS "Build the Catch2 test suite" ON)
option(DATALENS_BUILD_BENCH "Build the Phase-0 benchmark"  ON)
option(DATALENS_SANITIZERS  "Build with ASan + UBSan"       OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ── Core objects (compiled once, shared by the static and shared libs) ────────
add_library(datalens_objects OBJECT
    src/DataStore.cpp
    src/DataLensSchema.cpp
    src/DataLens.cpp
    src/c_api.cpp
    src/log.cpp)
target_include_directories(datalens_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(datalens_objects PUBLIC cxx_std_17)
set_target_properties(datalens_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(datalens_objects PRIVATE /W4)
else()
    target_compile_options(datalens_objects PRIVATE -Wall -Wextra)
endif()

if(DATALENS_SANITIZERS AND NOT MSVC)
    target_compile_options(datalens_objects PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(datalens_objects PUBLIC -fsanitize=address,undefined)
endif()

# Static library (primary; used by tests/bench and native engine integrations).
add_library(datalens STATIC $<TARGET_OBJECTS:datalens_objects>)
target_link_libraries(datalens PUBLIC datalens_objects)
add_library(datalens::datalens ALIAS datalens)

# Shared library (this is what Unity loads as libdatalens.so via P/Invoke once the
# C ABI lands in A7; for now it exports the C++ symbols).
add_library(datalens_shared SHARED $<TARGET_OBJECTS:datalens_objects>)
target_link_libraries(datalens_shared PUBLIC datalens_objects)
set_target_properties(datalens_shared PROPERTIES OUTPUT_NAME datalens)

# ── Install / export ─────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS datalens datalens_shared datalens_objects
    EXPORT datalensTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ── Tests / bench ────────────────────────────────────────────────────────────
if(DATALENS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(DATALENS_BUILD_BENCH)
    add_subdirectory(bench)
endif()
