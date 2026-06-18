include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(datalens_tests
    test_datastore.cpp
    test_schema.cpp
    test_view.cpp
    test_c_api.cpp
    test_system.cpp
    test_curve.cpp
    test_noise.cpp
    test_argmax.cpp
    test_lens.cpp
    test_ir.cpp
    test_schedule.cpp
    test_dataview.cpp)
target_link_libraries(datalens_tests PRIVATE datalens Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(datalens_tests)
