cmake_minimum_required(VERSION 3.20)
project(MacAndCheese VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MNC_BUILD_TESTS "Build Mac&Cheese tests" ON)

find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    include(FetchContent)
    message(STATUS "System zlib not found; fetching zlib 1.3.1 for Mac&Cheese DMG support")
    FetchContent_Declare(
        mnc_zlib
        URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(mnc_zlib)
    if (TARGET zlibstatic)
        set(MNC_ZLIB_TARGET zlibstatic)
    elseif (TARGET ZLIB::ZLIB)
        set(MNC_ZLIB_TARGET ZLIB::ZLIB)
    endif()
endif()

add_library(mnc_core
    src/macho.cpp
    src/bundle.cpp
    src/fd.cpp
    src/dmg.cpp
    src/hfsplus.cpp
    src/apfs.cpp
    src/dylib.cpp
    src/objc.cpp
    src/loader.cpp
    src/runtime.cpp
)
target_include_directories(mnc_core PUBLIC include)
target_compile_features(mnc_core PUBLIC cxx_std_17)
if (WIN32)
    target_compile_definitions(mnc_core PRIVATE NOMINMAX)
endif()
if (ZLIB_FOUND OR MNC_ZLIB_TARGET)
    if (ZLIB_FOUND)
        target_link_libraries(mnc_core PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(mnc_core PRIVATE ${MNC_ZLIB_TARGET})
    endif()
    target_compile_definitions(mnc_core PRIVATE MNC_HAS_ZLIB=1)
endif()

add_executable(mnc-inspect src/main.cpp)
target_link_libraries(mnc-inspect PRIVATE mnc_core)

add_executable(mnc-run src/runner.cpp)
target_link_libraries(mnc-run PRIVATE mnc_core)

if (MNC_BUILD_TESTS)
    enable_testing()
    add_executable(mnc-tests tests/test_macho.cpp)
    target_link_libraries(mnc-tests PRIVATE mnc_core)
    add_test(NAME mnc-tests COMMAND mnc-tests)
endif()

install(TARGETS mnc-inspect mnc-run RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
