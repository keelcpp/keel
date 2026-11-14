# ==============================================================================
# KeelCheckGuidelines.cmake
# Standalone CMake script (`cmake -DCONFIG_FILE=... -P KeelCheckGuidelines.cmake`)
# enforcing keel & downstream ecosystem coding guidelines:
#   1. Zero-STL & zero-<cstdint>/<cstddef> include discipline (with C++ <exception> allowed)
#   2. Fixed-width & pointer-sized keel::* integer discipline (no raw int/size_t/uint32_t)
#   3. Header hygiene (no `using namespace` in headers)
#   4. Optional standalone header compilation & transitive system-header leak check
# ==============================================================================

if(NOT DEFINED CONFIG_FILE OR NOT EXISTS "${CONFIG_FILE}")
    message(FATAL_ERROR "KeelCheckGuidelines.cmake: CONFIG_FILE must point to an existing config file.")
endif()

include("${CONFIG_FILE}")

# Strip C++11 raw string literals `R"delim(...)delim"` accurately using O(N) substring search
function(keel_strip_raw_strings INPUT_TEXT OUT_VAR)
    set(ACCUM "")
    set(REM "${INPUT_TEXT}")
    while(TRUE)
        string(FIND "${REM}" "R\"" POS)
        if(POS EQUAL -1)
            string(APPEND ACCUM "${REM}")
            break()
        endif()

        # Ensure `R` is not preceded by an identifier character (except valid prefixes u8, u, U, L)
        set(IS_RAW_PREFIX TRUE)
        if(POS GREATER 0)
            math(EXPR PREV1 "${POS} - 1")
            string(SUBSTRING "${REM}" ${PREV1} 1 CH1)
            if(CH1 MATCHES "[A-Za-z0-9_]")
                set(IS_RAW_PREFIX FALSE)
                if(CH1 STREQUAL "u" OR CH1 STREQUAL "U" OR CH1 STREQUAL "L")
                    if(PREV1 EQUAL 0)
                        set(IS_RAW_PREFIX TRUE)
                    else()
                        math(EXPR PREV2 "${PREV1} - 1")
                        string(SUBSTRING "${REM}" ${PREV2} 1 CH2)
                        if(NOT CH2 MATCHES "[A-Za-z0-9_]")
                            set(IS_RAW_PREFIX TRUE)
                        endif()
                    endif()
                elseif(CH1 STREQUAL "8" AND POS GREATER 1)
                    math(EXPR PREV2 "${POS} - 2")
                    string(SUBSTRING "${REM}" ${PREV2} 1 CH2)
                    if(CH2 STREQUAL "u")
                        if(PREV2 EQUAL 0)
                            set(IS_RAW_PREFIX TRUE)
                        else()
                            math(EXPR PREV3 "${POS} - 3")
                            string(SUBSTRING "${REM}" ${PREV3} 1 CH3)
                            if(NOT CH3 MATCHES "[A-Za-z0-9_]")
                                set(IS_RAW_PREFIX TRUE)
                            endif()
                        endif()
                    endif()
                endif()
            endif()
        endif()

        math(EXPR DELIM_START "${POS} + 2")
        if(NOT IS_RAW_PREFIX)
            string(SUBSTRING "${REM}" 0 ${DELIM_START} SKIP_HEAD)
            string(APPEND ACCUM "${SKIP_HEAD}")
            string(SUBSTRING "${REM}" ${DELIM_START} -1 REM)
            continue()
        endif()

        string(SUBSTRING "${REM}" ${DELIM_START} -1 AFTER_RQUOTE)
        string(FIND "${AFTER_RQUOTE}" "(" REL_OPEN_PAREN)
        if(REL_OPEN_PAREN EQUAL -1)
            string(APPEND ACCUM "${REM}")
            break()
        endif()

        if(REL_OPEN_PAREN GREATER 16)
            string(SUBSTRING "${REM}" 0 ${DELIM_START} SKIP_HEAD)
            string(APPEND ACCUM "${SKIP_HEAD}")
            string(SUBSTRING "${REM}" ${DELIM_START} -1 REM)
            continue()
        endif()

        if(REL_OPEN_PAREN GREATER 0)
            string(SUBSTRING "${AFTER_RQUOTE}" 0 ${REL_OPEN_PAREN} DELIM)
            if(DELIM MATCHES "[ \t\r\n\\\\)]")
                string(SUBSTRING "${REM}" 0 ${DELIM_START} SKIP_HEAD)
                string(APPEND ACCUM "${SKIP_HEAD}")
                string(SUBSTRING "${REM}" ${DELIM_START} -1 REM)
                continue()
            endif()
        else()
            set(DELIM "")
        endif()

        set(CLOSER ")${DELIM}\"")
        math(EXPR BODY_OFFSET "${REL_OPEN_PAREN} + 1")
        string(SUBSTRING "${AFTER_RQUOTE}" ${BODY_OFFSET} -1 BODY_AND_REST)
        string(FIND "${BODY_AND_REST}" "${CLOSER}" REL_CLOSE_POS)
        if(REL_CLOSE_POS EQUAL -1)
            string(APPEND ACCUM "${REM}")
            break()
        endif()

        string(SUBSTRING "${REM}" 0 ${POS} PREFIX_PART)
        string(APPEND ACCUM "${PREFIX_PART}\"\"")

        string(LENGTH "${CLOSER}" CLOSER_LEN)
        math(EXPR REST_OFFSET "${REL_CLOSE_POS} + ${CLOSER_LEN}")
        string(SUBSTRING "${BODY_AND_REST}" ${REST_OFFSET} -1 REM)
    endwhile()

    set(${OUT_VAR} "${ACCUM}" PARENT_SCOPE)
endfunction()

set(ALLOWED_HEADER_STD_INCLUDES
    "cassert"
    "cmath"
    "cstring"
    "exception"
    "initializer_list"
    "new"
)
if(KG_ALLOW_STDEXCEPT_IN_HEADERS)
    list(APPEND ALLOWED_HEADER_STD_INCLUDES "stdexcept")
endif()
if(KG_ALLOW_HEADERS)
    list(APPEND ALLOWED_HEADER_STD_INCLUDES ${KG_ALLOW_HEADERS})
endif()

set(FORBIDDEN_CPP_STD_INCLUDES
    "algorithm"
    "any"
    "array"
    "bitset"
    "chrono"
    "compare"
    "concepts"
    "cstddef"
    "cstdint"
    "deque"
    "filesystem"
    "format"
    "forward_list"
    "fstream"
    "functional"
    "iostream"
    "limits"
    "list"
    "map"
    "memory"
    "optional"
    "queue"
    "regex"
    "set"
    "source_location"
    "span"
    "sstream"
    "stack"
    "string"
    "string_view"
    "tuple"
    "type_traits"
    "typeindex"
    "unordered_map"
    "unordered_set"
    "utility"
    "variant"
    "vector"
)
if(NOT KG_ALLOW_STDEXCEPT)
    list(APPEND FORBIDDEN_CPP_STD_INCLUDES "stdexcept")
endif()
if(KG_ALLOW_HEADERS)
    list(REMOVE_ITEM FORBIDDEN_CPP_STD_INCLUDES ${KG_ALLOW_HEADERS})
endif()

set(FORBIDDEN_STD_TYPES_BASE
    "string|wstring|u8string|string_view|vector|span|optional|nullopt|array|function|unique_ptr|shared_ptr|weak_ptr|variant|monostate|unordered_map|unordered_set|map|set|source_location|type_index|chrono"
)
set(FORBIDDEN_STD_REGEX_HDR
    "std::(${FORBIDDEN_STD_TYPES_BASE}|runtime_error|logic_error|invalid_argument|out_of_range)([^A-Za-z0-9_]|$)"
)
if(KG_ALLOW_STDEXCEPT_IN_HEADERS)
    set(FORBIDDEN_STD_REGEX_HDR "std::(${FORBIDDEN_STD_TYPES_BASE})([^A-Za-z0-9_]|$)")
endif()

if(KG_ALLOW_STDEXCEPT)
    set(FORBIDDEN_STD_REGEX_CPP "std::(${FORBIDDEN_STD_TYPES_BASE})([^A-Za-z0-9_]|$)")
else()
    set(FORBIDDEN_STD_REGEX_CPP
        "std::(${FORBIDDEN_STD_TYPES_BASE}|runtime_error|logic_error|invalid_argument|out_of_range)([^A-Za-z0-9_]|$)"
    )
endif()

set(FORBIDDEN_RAW_INT_REGEX
    "(^|[^A-Za-z0-9_])(u?int(8|16|32|64|ptr|max)_t|size_t|ptrdiff_t|short|long|unsigned|signed|int)([^A-Za-z0-9_]|$)"
)

foreach(FILE_PATH IN LISTS KG_FILES)
    if(NOT EXISTS "${FILE_PATH}")
        continue()
    endif()

    get_filename_component(EXT "${FILE_PATH}" LAST_EXT)
    string(TOLOWER "${EXT}" EXT_LOWER)
    set(IS_HEADER FALSE)
    if(EXT_LOWER STREQUAL ".h" OR EXT_LOWER STREQUAL ".hpp" OR EXT_LOWER STREQUAL ".hh" OR EXT_LOWER STREQUAL ".hxx")
        set(IS_HEADER TRUE)
    endif()

    if(KG_SCOPE STREQUAL "HEADERS_ONLY" AND NOT IS_HEADER)
        continue()
    endif()

    file(RELATIVE_PATH REL_FILE "${KG_SOURCE_ROOT}" "${FILE_PATH}")
    if(REL_FILE MATCHES "^\\.\\.")
        set(REL_FILE "${FILE_PATH}")
    endif()

    file(READ "${FILE_PATH}" RAW_CONTENT)

    # 0. Honor per-line `// keel-lint: ignore` escape hatch
    string(FIND "${RAW_CONTENT}" "keel-lint:" HAS_LINT_PRAGMA)
    if(NOT HAS_LINT_PRAGMA EQUAL -1)
        string(REGEX REPLACE "[^\r\n]*//[ \t]*keel-lint:[ \t]*ignore[^\r\n]*" "" RAW_CONTENT "${RAW_CONTENT}")
    endif()

    # 1. Strip raw string literals `R"delim(...)delim"`
    string(FIND "${RAW_CONTENT}" "R\"" HAS_RAW_STR)
    if(NOT HAS_RAW_STR EQUAL -1)
        keel_strip_raw_strings("${RAW_CONTENT}" NO_RAW_CONTENT)
    else()
        set(NO_RAW_CONTENT "${RAW_CONTENT}")
    endif()

    # 2. Single-pass unrolled-loop comment + string + char literal stripping:
    #    Uses Friedl's unrolled-loop pattern (`[^"\\]*(\\.[^"\\]*)*` and `/\*[^*]*\*+([^/*][^*]*\*+)*/`)
    #    so CMake's NFA engine runs in flat O(N) loops with zero per-character stack recursion on large files,
    #    while preventing `//` inside `"https://..."` from corrupting quotes.
    string(REGEX REPLACE
        "operator[ \t]*\"\"[ \t]*_[A-Za-z0-9_]*[ \t]*\\([ \t]*(unsigned[ \t]+long[ \t]+long|long[ \t]+double)([^)]*)\\)"
        "operator_udl()"
        PRE_CLEAN
        "${NO_RAW_CONTENT}"
    )
    string(REGEX REPLACE
        "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*|\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(\\\\.[^'\\\\]*)*')"
        " "
        CLEAN_CODE
        "${PRE_CLEAN}"
    )

    # Check explicit FORBID_HEADERS across both `<...>` and `"..."` includes
    if(KG_FORBID_HEADERS)
        string(REGEX REPLACE "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\r\n]*)" " " NO_COMMENTS_FOR_INC "${NO_RAW_CONTENT}")
        string(REGEX MATCHALL "(^|[\r\n])[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" ALL_INC_LINES "${NO_COMMENTS_FOR_INC}")
        foreach(INC_LINE IN LISTS ALL_INC_LINES)
            string(REGEX REPLACE "^.*[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" INC_NAME "${INC_LINE}")
            if(INC_NAME IN_LIST KG_FORBID_HEADERS AND NOT INC_NAME IN_LIST KG_ALLOW_HEADERS)
                message(FATAL_ERROR
                    "[keel-guidelines] Target '${KG_TARGET}' violation in ${REL_FILE}: "
                    "forbidden '#include <${INC_NAME}>'. Use high-level ecosystem abstractions or add to ALLOW_HEADERS."
                )
            endif()
        endforeach()
    endif()

    if(KG_BAN_STL)
        string(REGEX MATCHALL "(^|[\r\n])[ \t]*#[ \t]*include[ \t]*<[^>]+>" ANGLE_INC_LINES "${CLEAN_CODE}")
        foreach(INC_LINE IN LISTS ANGLE_INC_LINES)
            string(REGEX REPLACE "^.*[ \t]*#[ \t]*include[ \t]*<([^>]+)>.*$" "\\1" INC_NAME "${INC_LINE}")
            if(IS_HEADER)
                if(NOT INC_NAME IN_LIST ALLOWED_HEADER_STD_INCLUDES)
                    if(INC_NAME STREQUAL "cstdint" OR INC_NAME STREQUAL "cstddef")
                        message(FATAL_ERROR
                            "[keel-guidelines] Target '${KG_TARGET}' header violation in ${REL_FILE}: "
                            "forbidden '#include <${INC_NAME}>'. Include \"keel/core.h\" and use keel::i8..i64 / keel::u8..u64 / keel::isize / keel::usize."
                        )
                    elseif(INC_NAME STREQUAL "stdexcept")
                        message(FATAL_ERROR
                            "[keel-guidelines] Target '${KG_TARGET}' header violation in ${REL_FILE}: "
                            "'#include <stdexcept>' pulls in <string>. Use <exception> or \"keel/exception.h\" (keel::Exception) in headers."
                        )
                    else()
                        message(FATAL_ERROR
                            "[keel-guidelines] Target '${KG_TARGET}' header violation in ${REL_FILE}: "
                            "forbidden '#include <${INC_NAME}>'. Allowed angle-bracket includes in headers: <cassert>, <cmath>, <cstring>, <exception>, <initializer_list>, <new>."
                        )
                    endif()
                endif()
            else()
                if(INC_NAME IN_LIST FORBIDDEN_CPP_STD_INCLUDES)
                    if(INC_NAME STREQUAL "cstdint" OR INC_NAME STREQUAL "cstddef")
                        message(FATAL_ERROR
                            "[keel-guidelines] Target '${KG_TARGET}' violation in ${REL_FILE}: "
                            "forbidden '#include <${INC_NAME}>'. Include \"keel/core.h\" and use keel::i8..i64 / keel::u8..u64 / keel::isize / keel::usize."
                        )
                    else()
                        message(FATAL_ERROR
                            "[keel-guidelines] Target '${KG_TARGET}' violation in ${REL_FILE}: "
                            "forbidden STL header '#include <${INC_NAME}>'. Use keel equivalents (\"keel/array.h\", \"keel/string.h\", \"keel/hash_map.h\", \"keel/exception.h\", etc.)."
                        )
                    endif()
                endif()
            endif()
        endforeach()
    endif()

    if(IS_HEADER)
        if(CLEAN_CODE MATCHES "(^|[^A-Za-z0-9_])using[ \t]+namespace[ \t]+")
            message(FATAL_ERROR
                "[keel-guidelines] Target '${KG_TARGET}' header violation in ${REL_FILE}: "
                "'using namespace' is forbidden in headers."
            )
        endif()
    endif()

    if(KG_BAN_STL)
        if(IS_HEADER)
            set(ACTIVE_STD_REGEX "${FORBIDDEN_STD_REGEX_HDR}")
        else()
            set(ACTIVE_STD_REGEX "${FORBIDDEN_STD_REGEX_CPP}")
        endif()
        if(CLEAN_CODE MATCHES "${ACTIVE_STD_REGEX}")
            string(REGEX MATCH "${ACTIVE_STD_REGEX}" MATCHED_STD "${CLEAN_CODE}")
            string(STRIP "${MATCHED_STD}" MATCHED_STD)
            message(FATAL_ERROR
                "[keel-guidelines] Target '${KG_TARGET}' violation in ${REL_FILE}: "
                "forbidden STL symbol '${MATCHED_STD}' (use keel::* instead)."
            )
        endif()
    endif()

    if(KG_BAN_RAW_INTS)
        # Allow ISO C++ standard-mandated `int main(...)` and postfix `operator++(int)` / `operator--(int)`
        set(INT_CHECK_CODE "${CLEAN_CODE}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*[,\\)])" "\\1keel_main(keel_argc," INT_CHECK_CODE "${INT_CHECK_CODE}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\()" "\\1keel_main(" INT_CHECK_CODE "${INT_CHECK_CODE}")
        string(REGEX REPLACE "operator[ \t]*(\\+\\+|--)[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*\\)" "operator_postinc()" INT_CHECK_CODE "${INT_CHECK_CODE}")

        if(INT_CHECK_CODE MATCHES "${FORBIDDEN_RAW_INT_REGEX}")
            string(REGEX MATCH "${FORBIDDEN_RAW_INT_REGEX}" MATCHED_INT "${INT_CHECK_CODE}")
            string(STRIP "${MATCHED_INT}" MATCHED_INT)
            message(FATAL_ERROR
                "[keel-guidelines] Target '${KG_TARGET}' violation in ${REL_FILE}: "
                "forbidden raw integer token '${MATCHED_INT}'. Use keel::i8..i64, keel::u8..u64, keel::isize, or keel::usize "
                "(annotate true C/OS ABI boundary lines with '// keel-lint: ignore')."
            )
        endif()
    endif()
endforeach()

# 4. Optional standalone header compilation & transitive system header check
if(RUN_STANDALONE_HEADERS AND KG_HEADERS_DIRS AND DEFINED KG_COMPILER_PATH AND EXISTS "${KG_COMPILER_PATH}")
    set(SCRATCH_DIR "${KG_BINARY_DIR}/keel_guidelines/${KG_TARGET}_standalone")
    file(MAKE_DIRECTORY "${SCRATCH_DIR}")

    set(SYS_INC_FLAGS "")
    if(KG_STD_INC_DIRS)
        foreach(SDIR IN LISTS KG_STD_INC_DIRS)
            if(KG_COMPILER_ID MATCHES "MSVC")
                list(APPEND SYS_INC_FLAGS "/I${SDIR}")
            else()
                list(APPEND SYS_INC_FLAGS "-isystem" "${SDIR}")
            endif()
        endforeach()
    endif()

    if(KG_COMPILER_ID MATCHES "MSVC" AND "$ENV{INCLUDE}" STREQUAL "" AND SYS_INC_FLAGS STREQUAL "")
        get_filename_component(CL_DIR1 "${KG_COMPILER_PATH}" DIRECTORY)
        get_filename_component(CL_DIR2 "${CL_DIR1}" DIRECTORY)
        get_filename_component(CL_DIR3 "${CL_DIR2}" DIRECTORY)
        get_filename_component(MSVC_ROOT "${CL_DIR3}" DIRECTORY)
        if(EXISTS "${MSVC_ROOT}/include")
            list(APPEND SYS_INC_FLAGS "/I${MSVC_ROOT}/include")
        endif()
        file(GLOB WIN_SDK_VERSIONS "C:/Program Files (x86)/Windows Kits/10/Include/10.*")
        if(WIN_SDK_VERSIONS)
            list(SORT WIN_SDK_VERSIONS ORDER DESCENDING)
            list(GET WIN_SDK_VERSIONS 0 LATEST_SDK)
            if(EXISTS "${LATEST_SDK}/ucrt")
                list(APPEND SYS_INC_FLAGS "/I${LATEST_SDK}/ucrt")
            endif()
            if(EXISTS "${LATEST_SDK}/shared")
                list(APPEND SYS_INC_FLAGS "/I${LATEST_SDK}/shared")
            endif()
        endif()
    endif()

    # Baseline includes allowed transitively (keel/core.h uses <cstddef> and <cstdint> internally)
    set(BASELINE_SRC "${SCRATCH_DIR}/baseline.cpp")
    set(BASELINE_CODE
        "#include <cassert>\n#include <cmath>\n#include <cstddef>\n#include <cstdint>\n#include <cstring>\n#include <exception>\n#include <initializer_list>\n#include <new>\n"
    )
    if(KG_ALLOW_STDEXCEPT_IN_HEADERS)
        string(APPEND BASELINE_CODE "#include <stdexcept>\n")
    endif()
    foreach(EXTRA_HDR IN LISTS KG_ALLOW_HEADERS)
        string(APPEND BASELINE_CODE "#include <${EXTRA_HDR}>\n")
    endforeach()
    string(APPEND BASELINE_CODE "int main() { return 0; }\n")
    file(WRITE "${BASELINE_SRC}" "${BASELINE_CODE}")

    if(KG_COMPILER_ID MATCHES "MSVC")
        execute_process(
            COMMAND "${KG_COMPILER_PATH}" /nologo /std:c++20 /Zs /showIncludes ${SYS_INC_FLAGS} "${BASELINE_SRC}"
            OUTPUT_VARIABLE BASELINE_OUT
            ERROR_VARIABLE BASELINE_ERR
            RESULT_VARIABLE BASELINE_RES
        )
    else()
        execute_process(
            COMMAND "${KG_COMPILER_PATH}" -std=c++20 -fsyntax-only -H ${SYS_INC_FLAGS} "${BASELINE_SRC}"
            OUTPUT_VARIABLE BASELINE_OUT
            ERROR_VARIABLE BASELINE_ERR
            RESULT_VARIABLE BASELINE_RES
        )
    endif()

    set(COMBINED_BASELINE "${BASELINE_OUT}\n${BASELINE_ERR}")
    if(NOT BASELINE_RES EQUAL 0)
        message(FATAL_ERROR "[keel-guidelines] Failed to compile baseline for '${KG_TARGET}':\n${COMBINED_BASELINE}")
    endif()

    set(ALLOWED_SYS_PATHS "")
    string(REPLACE "\r\n" "\n" COMBINED_BASELINE "${COMBINED_BASELINE}")
    string(REPLACE "\n" ";" BASELINE_LINES "${COMBINED_BASELINE}")
    foreach(BLINE IN LISTS BASELINE_LINES)
        if(KG_COMPILER_ID MATCHES "MSVC")
            if(BLINE MATCHES "Note: including file:[ \t]*(.+)$")
                string(STRIP "${CMAKE_MATCH_1}" INC_PATH)
                file(TO_CMAKE_PATH "${INC_PATH}" INC_PATH_NORM)
                string(TOLOWER "${INC_PATH_NORM}" INC_PATH_NORM)
                list(APPEND ALLOWED_SYS_PATHS "${INC_PATH_NORM}")
            endif()
        else()
            if(BLINE MATCHES "^\\.+ ([^ ]+)$")
                string(STRIP "${CMAKE_MATCH_1}" INC_PATH)
                file(TO_CMAKE_PATH "${INC_PATH}" INC_PATH_NORM)
                list(APPEND ALLOWED_SYS_PATHS "${INC_PATH_NORM}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES ALLOWED_SYS_PATHS)

    set(INC_ROOT_FLAGS "")
    set(NORM_ALLOWED_ROOTS "")
    foreach(ROOT_DIR IN LISTS KG_ALLOWED_INCLUDE_ROOTS)
        if(EXISTS "${ROOT_DIR}")
            if(KG_COMPILER_ID MATCHES "MSVC")
                list(APPEND INC_ROOT_FLAGS "/I${ROOT_DIR}")
            else()
                list(APPEND INC_ROOT_FLAGS "-I${ROOT_DIR}")
            endif()
            file(TO_CMAKE_PATH "${ROOT_DIR}" NORM_ROOT)
            if(KG_COMPILER_ID MATCHES "MSVC")
                string(TOLOWER "${NORM_ROOT}" NORM_ROOT)
            endif()
            list(APPEND NORM_ALLOWED_ROOTS "${NORM_ROOT}")
        endif()
    endforeach()

    set(STANDALONE_COUNT 0)
    foreach(HDIR IN LISTS KG_HEADERS_DIRS)
        file(GLOB_RECURSE DIR_HDRS "${HDIR}/*.h" "${HDIR}/*.hpp")
        list(SORT DIR_HDRS)
        foreach(HDR IN LISTS DIR_HDRS)
            set(SKIP_HDR FALSE)
            foreach(EX_PAT IN LISTS KG_EXCLUDE_FILES)
                if(HDR MATCHES "${EX_PAT}")
                    set(SKIP_HDR TRUE)
                    break()
                endif()
            endforeach()
            if(SKIP_HDR)
                continue()
            endif()

            file(RELATIVE_PATH HDR_REL "${HDIR}" "${HDR}")
            string(REGEX REPLACE "[^A-Za-z0-9_]" "_" SAFE_NAME "${HDR_REL}")
            set(TEST_SRC "${SCRATCH_DIR}/check_${SAFE_NAME}.cpp")
            file(WRITE "${TEST_SRC}" "#include \"${HDR_REL}\"\nint main() { return 0; }\n")

            if(KG_COMPILER_ID MATCHES "MSVC")
                execute_process(
                    COMMAND "${KG_COMPILER_PATH}" /nologo /std:c++20 /Zs /showIncludes ${SYS_INC_FLAGS} ${INC_ROOT_FLAGS} "${TEST_SRC}"
                    OUTPUT_VARIABLE HDR_OUT
                    ERROR_VARIABLE HDR_ERR
                    RESULT_VARIABLE HDR_RES
                )
            else()
                execute_process(
                    COMMAND "${KG_COMPILER_PATH}" -std=c++20 -fsyntax-only -H ${SYS_INC_FLAGS} ${INC_ROOT_FLAGS} "${TEST_SRC}"
                    OUTPUT_VARIABLE HDR_OUT
                    ERROR_VARIABLE HDR_ERR
                    RESULT_VARIABLE HDR_RES
                )
            endif()

            set(COMBINED_HDR "${HDR_OUT}\n${HDR_ERR}")
            if(NOT HDR_RES EQUAL 0)
                message(FATAL_ERROR "[keel-guidelines] Standalone compilation failed for ${HDR_REL}:\n${COMBINED_HDR}")
            endif()

            string(REPLACE "\r\n" "\n" COMBINED_HDR "${COMBINED_HDR}")
            string(REPLACE "\n" ";" HDR_LINES "${COMBINED_HDR}")
            foreach(HLINE IN LISTS HDR_LINES)
                set(EXTRACTED "")
                if(KG_COMPILER_ID MATCHES "MSVC")
                    if(HLINE MATCHES "Note: including file:[ \t]*(.+)$")
                        string(STRIP "${CMAKE_MATCH_1}" EXTRACTED)
                        file(TO_CMAKE_PATH "${EXTRACTED}" EXTRACTED)
                        string(TOLOWER "${EXTRACTED}" EXTRACTED)
                    endif()
                else()
                    if(HLINE MATCHES "^\\.+ ([^ ]+)$")
                        string(STRIP "${CMAKE_MATCH_1}" EXTRACTED)
                        file(TO_CMAKE_PATH "${EXTRACTED}" EXTRACTED)
                    endif()
                endif()

                if(NOT EXTRACTED STREQUAL "")
                    set(IN_ALLOWED_ROOT FALSE)
                    foreach(NROOT IN LISTS NORM_ALLOWED_ROOTS)
                        string(FIND "${EXTRACTED}" "${NROOT}" POS_ROOT)
                        if(POS_ROOT EQUAL 0)
                            set(IN_ALLOWED_ROOT TRUE)
                            break()
                        endif()
                    endforeach()

                    if(NOT IN_ALLOWED_ROOT AND NOT EXTRACTED IN_LIST ALLOWED_SYS_PATHS)
                        message(FATAL_ERROR
                            "[keel-guidelines] Transitive header purity violation in ${HDR_REL}: "
                            "unexpected system header '${EXTRACTED}'"
                        )
                    endif()
                endif()
            endforeach()

            math(EXPR STANDALONE_COUNT "${STANDALONE_COUNT} + 1")
        endforeach()
    endforeach()

    message(STATUS "[keel-guidelines] Standalone header purity passed for '${KG_TARGET}' (${STANDALONE_COUNT} headers).")
endif()

if(DEFINED STAMP_FILE AND NOT "${STAMP_FILE}" STREQUAL "")
    get_filename_component(STAMP_DIR "${STAMP_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${STAMP_DIR}")
    file(WRITE "${STAMP_FILE}" "ok\n")
endif()
