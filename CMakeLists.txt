cmake_minimum_required(VERSION 3.20)
project(keel VERSION 0.1.0 LANGUAGES CXX)

option(KEEL_UNITY_BUILD "Compile keel as a single unity translation unit (src/keel_unity.cpp)" OFF)
option(KEEL_NO_EXCEPTIONS_RTTI "Compile keel without C++ exceptions and RTTI" OFF)
option(KEEL_BUILD_TESTS "Build keel unit tests and header purity checks" ${PROJECT_IS_TOP_LEVEL})
option(KEEL_BUILD_BENCHMARKS "Build keel benchmarks against std" ${PROJECT_IS_TOP_LEVEL})

if(KEEL_UNITY_BUILD)
    set(KEEL_SOURCES src/keel_unity.cpp)
else()
    set(KEEL_SOURCES
        src/core.cpp
        src/memory.cpp
        src/utf8.cpp
        src/number.cpp
        src/format.cpp
        src/io.cpp
        src/fs.cpp
    )
endif()

add_library(keel STATIC ${KEEL_SOURCES})
add_library(keel::keel ALIAS keel)

target_compile_features(keel PUBLIC cxx_std_20)
set_target_properties(keel PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(keel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(keel PRIVATE /W4 /utf-8)
    if(KEEL_NO_EXCEPTIONS_RTTI)
        string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        target_compile_options(keel PUBLIC /EHs-c- /GR-)
        target_compile_definitions(keel PUBLIC _HAS_EXCEPTIONS=0)
    endif()
else()
    target_compile_options(keel PRIVATE -Wall -Wextra -Wpedantic)
    if(KEEL_NO_EXCEPTIONS_RTTI)
        target_compile_options(keel PUBLIC -fno-exceptions -fno-rtti)
    endif()
endif()

# Install & package support (`find_package(keel)`)
include(CMakePackageConfigHelpers)
install(TARGETS keel EXPORT keelTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/keel DESTINATION include)
install(EXPORT keelTargets
    FILE keelTargets.cmake
    NAMESPACE keel::
    DESTINATION lib/cmake/keel
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/keelConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/keelConfig.cmake"
    INSTALL_DESTINATION lib/cmake/keel
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/keelConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/keelConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/keelConfigVersion.cmake"
    DESTINATION lib/cmake/keel
)

if(KEEL_BUILD_TESTS)
    enable_testing()

    add_executable(test_keel tests/test_keel.cpp)
    target_link_libraries(test_keel PRIVATE keel::keel)
    if(MSVC)
        target_compile_options(test_keel PRIVATE /W4 /utf-8)
    else()
        target_compile_options(test_keel PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME test_keel COMMAND test_keel)

    string(REPLACE ";" "|" KEEL_STD_INC_JOINED "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")
    add_test(
        NAME test_header_purity
        COMMAND ${CMAKE_COMMAND}
            "-DKEEL_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include"
            "-DCOMPILER_PATH=${CMAKE_CXX_COMPILER}"
            "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DSTD_INC_DIRS=${KEEL_STD_INC_JOINED}"
            "-DCMAKE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckHeaderPurity.cmake"
    )
endif()

if(KEEL_BUILD_BENCHMARKS)
    add_executable(bench_keel benchmarks/bench_keel.cpp)
    target_link_libraries(bench_keel PRIVATE keel::keel)
    if(MSVC)
        target_compile_options(bench_keel PRIVATE /W4 /utf-8)
    else()
        target_compile_options(bench_keel PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
