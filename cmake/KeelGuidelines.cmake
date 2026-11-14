# ==============================================================================
# KeelGuidelines.cmake
# Opt-in CMake enforcement of keel ecosystem coding guidelines for libraries
# and downstream applications:
#   - Zero-STL & zero-<cstdint>/<cstddef> discipline (`keel::*` types & containers)
#   - First-class C++ exception support (`<exception>` / `keel::Exception` allowed;
#     never forces `-fno-exceptions`; `<stdexcept>` allowed in `.cpp` by default)
#   - Fine-grained escape hatches (`// keel-lint: ignore`, `EXCLUDE_FILES`, `ALLOW_HEADERS`)
#   - Incremental pre-compile build check + optional standalone CTest header audit
# ==============================================================================

include_guard(GLOBAL)

set(_KEEL_GUIDELINES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Directory containing KeelGuidelines.cmake")

function(keel_enforce_guidelines TARGET_NAME)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR "keel_enforce_guidelines: target '${TARGET_NAME}' does not exist.")
    endif()

    set(OPTIONS "")
    set(ONE_VALUE_ARGS
        SCOPE
        BAN_STL
        BAN_RAW_INTS
        ALLOW_STDEXCEPT
        ALLOW_STDEXCEPT_IN_HEADERS
        STANDALONE_HEADERS
        WARNINGS
        WARNINGS_AS_ERRORS
        TEST_NAME
    )
    set(MULTI_VALUE_ARGS
        HEADERS_DIR
        ALLOW_HEADERS
        FORBID_HEADERS
        EXCLUDE_FILES
        ALLOWED_INCLUDE_ROOTS
    )
    cmake_parse_arguments(KG "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})

    if(NOT DEFINED KG_SCOPE)
        set(KG_SCOPE "ALL")
    endif()
    if(NOT KG_SCOPE STREQUAL "ALL" AND NOT KG_SCOPE STREQUAL "HEADERS_ONLY")
        message(FATAL_ERROR "keel_enforce_guidelines(${TARGET_NAME}): SCOPE must be ALL or HEADERS_ONLY (got '${KG_SCOPE}').")
    endif()

    if(NOT DEFINED KG_BAN_STL)
        set(KG_BAN_STL ON)
    endif()
    if(NOT DEFINED KG_BAN_RAW_INTS)
        set(KG_BAN_RAW_INTS ON)
    endif()
    if(NOT DEFINED KG_ALLOW_STDEXCEPT)
        set(KG_ALLOW_STDEXCEPT ON)
    endif()
    if(NOT DEFINED KG_ALLOW_STDEXCEPT_IN_HEADERS)
        set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
    endif()
    if(NOT DEFINED KG_STANDALONE_HEADERS)
        set(KG_STANDALONE_HEADERS OFF)
    endif()
    if(NOT DEFINED KG_WARNINGS)
        set(KG_WARNINGS ON)
    endif()
    if(NOT DEFINED KG_WARNINGS_AS_ERRORS)
        set(KG_WARNINGS_AS_ERRORS OFF)
    endif()

    get_target_property(_TARGET_TYPE "${TARGET_NAME}" TYPE)

    # 1. Apply PRIVATE compiler warning flags without altering exception/RTTI settings
    if(KG_WARNINGS AND NOT _TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        if(MSVC)
            target_compile_options("${TARGET_NAME}" PRIVATE /W4 /utf-8)
            if(KG_WARNINGS_AS_ERRORS)
                target_compile_options("${TARGET_NAME}" PRIVATE /WX)
            endif()
        else()
            target_compile_options("${TARGET_NAME}" PRIVATE -Wall -Wextra -Wpedantic)
            if(KG_WARNINGS_AS_ERRORS)
                target_compile_options("${TARGET_NAME}" PRIVATE -Werror)
            endif()
        endif()
    endif()

    # 2. Collect headers from HEADERS_DIR and sources from TARGET_NAME
    set(_CANDIDATE_FILES "")
    set(_ABS_HEADER_DIRS "")

    foreach(HDIR IN LISTS KG_HEADERS_DIR)
        get_filename_component(_ABS_HDIR "${HDIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${_ABS_HDIR}")
            list(APPEND _ABS_HEADER_DIRS "${_ABS_HDIR}")
            file(GLOB_RECURSE _HDR_FILES "${_ABS_HDIR}/*.h" "${_ABS_HDIR}/*.hpp" "${_ABS_HDIR}/*.hh" "${_ABS_HDIR}/*.hxx")
            list(APPEND _CANDIDATE_FILES ${_HDR_FILES})
        endif()
    endforeach()

    if(NOT _TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
        get_target_property(_TGT_SOURCE_DIR "${TARGET_NAME}" SOURCE_DIR)
        get_target_property(_TGT_BINARY_DIR "${TARGET_NAME}" BINARY_DIR)
        get_target_property(_TGT_SOURCES "${TARGET_NAME}" SOURCES)
        if(_TGT_SOURCES)
            foreach(SRC IN LISTS _TGT_SOURCES)
                if(SRC MATCHES "^\\$<")
                    continue()
                endif()
                get_filename_component(_ABS_SRC "${SRC}" ABSOLUTE BASE_DIR "${_TGT_SOURCE_DIR}")
                # Skip build-generated sources inside binary dir
                string(FIND "${_ABS_SRC}" "${_TGT_BINARY_DIR}/" _IN_BIN_DIR)
                if(_IN_BIN_DIR EQUAL 0)
                    continue()
                endif()
                if(EXISTS "${_ABS_SRC}")
                    list(APPEND _CANDIDATE_FILES "${_ABS_SRC}")
                endif()
            endforeach()
        endif()
    endif()

    list(REMOVE_DUPLICATES _CANDIDATE_FILES)
    list(SORT _CANDIDATE_FILES)

    set(_CHECKED_FILES "")
    foreach(FPATH IN LISTS _CANDIDATE_FILES)
        file(TO_CMAKE_PATH "${FPATH}" _NORM_FPATH)
        set(_EXCLUDED FALSE)
        foreach(EX_PAT IN LISTS KG_EXCLUDE_FILES)
            file(TO_CMAKE_PATH "${EX_PAT}" _NORM_EX)
            if(_NORM_FPATH MATCHES "${_NORM_EX}")
                set(_EXCLUDED TRUE)
                break()
            endif()
        endforeach()
        if(NOT _EXCLUDED)
            list(APPEND _CHECKED_FILES "${_NORM_FPATH}")
        endif()
    endforeach()

    # Resolve keel include directory automatically for standalone header compilation
    set(_ALLOWED_ROOTS ${_ABS_HEADER_DIRS})
    foreach(EXTRA_ROOT IN LISTS KG_ALLOWED_INCLUDE_ROOTS)
        get_filename_component(_ABS_ROOT "${EXTRA_ROOT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _ALLOWED_ROOTS "${_ABS_ROOT}")
    endforeach()
    if(TARGET keel::keel)
        get_target_property(_KEEL_INC_DIRS keel::keel INTERFACE_INCLUDE_DIRECTORIES)
        if(_KEEL_INC_DIRS)
            foreach(_KINC IN LISTS _KEEL_INC_DIRS)
                string(REGEX REPLACE "^\\$<BUILD_INTERFACE:([^>]+)>$" "\\1" _KINC_CLEAN "${_KINC}")
                if(NOT _KINC_CLEAN MATCHES "^\\$<" AND EXISTS "${_KINC_CLEAN}")
                    list(APPEND _ALLOWED_ROOTS "${_KINC_CLEAN}")
                endif()
            endforeach()
        endif()
    endif()
    if(EXISTS "${_KEEL_GUIDELINES_CMAKE_DIR}/../include/keel/core.h")
        get_filename_component(_LOCAL_KEEL_INC "${_KEEL_GUIDELINES_CMAKE_DIR}/../include" ABSOLUTE)
        list(APPEND _ALLOWED_ROOTS "${_LOCAL_KEEL_INC}")
    elseif(EXISTS "${_KEEL_GUIDELINES_CMAKE_DIR}/../../../include/keel/core.h")
        get_filename_component(_INSTALL_KEEL_INC "${_KEEL_GUIDELINES_CMAKE_DIR}/../../../include" ABSOLUTE)
        list(APPEND _ALLOWED_ROOTS "${_INSTALL_KEEL_INC}")
    endif()
    list(REMOVE_DUPLICATES _ALLOWED_ROOTS)

    # 3. Write per-target configuration file (avoids OS command-line length & semicolon escaping limits)
    set(_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/keel_guidelines")
    file(MAKE_DIRECTORY "${_WORK_DIR}")
    set(_CONFIG_FILE "${_WORK_DIR}/${TARGET_NAME}_config.cmake")
    set(_STAMP_FILE "${_WORK_DIR}/${TARGET_NAME}.stamp")

    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _NORM_SRC_ROOT)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}" _NORM_BIN_ROOT)
    file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" _NORM_COMPILER)

    set(_NORM_STD_INC "")
    foreach(SDIR IN LISTS CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES)
        file(TO_CMAKE_PATH "${SDIR}" _NSDIR)
        list(APPEND _NORM_STD_INC "${_NSDIR}")
    endforeach()

    set(_NORM_EXCLUDE "")
    foreach(EX_PAT IN LISTS KG_EXCLUDE_FILES)
        file(TO_CMAKE_PATH "${EX_PAT}" _NEX)
        list(APPEND _NORM_EXCLUDE "${_NEX}")
    endforeach()

    set(_CFG_CONTENT "# Auto-generated by keel_enforce_guidelines(${TARGET_NAME})\n")
    string(APPEND _CFG_CONTENT "set(KG_TARGET \"${TARGET_NAME}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_SCOPE \"${KG_SCOPE}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_BAN_STL ${KG_BAN_STL})\n")
    string(APPEND _CFG_CONTENT "set(KG_BAN_RAW_INTS ${KG_BAN_RAW_INTS})\n")
    string(APPEND _CFG_CONTENT "set(KG_ALLOW_STDEXCEPT ${KG_ALLOW_STDEXCEPT})\n")
    string(APPEND _CFG_CONTENT "set(KG_ALLOW_STDEXCEPT_IN_HEADERS ${KG_ALLOW_STDEXCEPT_IN_HEADERS})\n")
    string(APPEND _CFG_CONTENT "set(KG_SOURCE_ROOT \"${_NORM_SRC_ROOT}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_BINARY_DIR \"${_NORM_BIN_ROOT}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_COMPILER_PATH \"${_NORM_COMPILER}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_COMPILER_ID \"${CMAKE_CXX_COMPILER_ID}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_STD_INC_DIRS \"${_NORM_STD_INC}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_ALLOW_HEADERS \"${KG_ALLOW_HEADERS}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_FORBID_HEADERS \"${KG_FORBID_HEADERS}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_EXCLUDE_FILES \"${_NORM_EXCLUDE}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_HEADERS_DIRS \"${_ABS_HEADER_DIRS}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_ALLOWED_INCLUDE_ROOTS \"${_ALLOWED_ROOTS}\")\n")
    string(APPEND _CFG_CONTENT "set(KG_FILES\n")
    foreach(CF IN LISTS _CHECKED_FILES)
        string(APPEND _CFG_CONTENT "    \"${CF}\"\n")
    endforeach()
    string(APPEND _CFG_CONTENT ")\n")

    file(WRITE "${_CONFIG_FILE}.tmp" "${_CFG_CONTENT}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_CONFIG_FILE}.tmp" "${_CONFIG_FILE}"
    )
    file(REMOVE "${_CONFIG_FILE}.tmp")

    set(_CHECK_SCRIPT "${_KEEL_GUIDELINES_CMAKE_DIR}/KeelCheckGuidelines.cmake")

    # 4. Incremental build-time lint target (0 ms on no-op incremental builds)
    add_custom_command(
        OUTPUT "${_STAMP_FILE}"
        COMMAND "${CMAKE_COMMAND}"
            "-DCONFIG_FILE=${_CONFIG_FILE}"
            "-DRUN_STANDALONE_HEADERS=OFF"
            "-DSTAMP_FILE=${_STAMP_FILE}"
            -P "${_CHECK_SCRIPT}"
        DEPENDS ${_CHECKED_FILES} "${_CONFIG_FILE}" "${_CHECK_SCRIPT}"
        COMMENT "Checking keel guidelines for ${TARGET_NAME}"
        VERBATIM
    )
    add_custom_target("${TARGET_NAME}_keel_guidelines" DEPENDS "${_STAMP_FILE}")
    add_dependencies("${TARGET_NAME}" "${TARGET_NAME}_keel_guidelines")

    # 5. Optional CTest standalone header compilation & transitive system header check
    if(KG_STANDALONE_HEADERS AND _ABS_HEADER_DIRS)
        if(DEFINED KG_TEST_NAME AND NOT "${KG_TEST_NAME}" STREQUAL "")
            set(_PURITY_TEST_NAME "${KG_TEST_NAME}")
        else()
            set(_PURITY_TEST_NAME "${TARGET_NAME}_header_purity")
        endif()
        add_test(
            NAME "${_PURITY_TEST_NAME}"
            COMMAND "${CMAKE_COMMAND}"
                "-DCONFIG_FILE=${_CONFIG_FILE}"
                "-DRUN_STANDALONE_HEADERS=ON"
                -P "${_CHECK_SCRIPT}"
        )
    endif()
endfunction()
