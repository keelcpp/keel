# ==============================================================================
# KeelCheckGuidelines.cmake
# Standalone CMake script (`cmake -DCONFIG_FILE=... -P KeelCheckGuidelines.cmake`)
# enforcing keel & downstream ecosystem coding guidelines:
#   1. Zero-STL & zero-<cstdint>/<cstddef> include discipline (with C++ <exception> allowed)
#   2. Fixed-width & pointer-sized keel::* integer discipline (no raw int/size_t/uint32_t)
#   3. Header hygiene (no `using namespace` in headers; angle-bracket project prefixes)
#   4. Per-file mtime/size incremental cache + full multi-violation reporting with file:line
#   5. Optional standalone header compilation & transitive system-header leak check
# ==============================================================================

if(NOT DEFINED CONFIG_FILE OR NOT EXISTS "${CONFIG_FILE}")
    message(FATAL_ERROR "KeelCheckGuidelines.cmake: CONFIG_FILE must point to an existing config file.")
endif()

include("${CONFIG_FILE}")

# Normalize KG_BAN_RAW_INTS to ALL / HEADERS_ONLY / OFF (backward-compatible with ON/OFF)
if(NOT DEFINED KG_BAN_RAW_INTS)
    set(_BAN_RAW_INTS_MODE "ALL")
else()
    string(TOUPPER "${KG_BAN_RAW_INTS}" _BRI_UPPER)
    if(_BRI_UPPER STREQUAL "ON" OR _BRI_UPPER STREQUAL "TRUE" OR _BRI_UPPER STREQUAL "1" OR _BRI_UPPER STREQUAL "ALL")
        set(_BAN_RAW_INTS_MODE "ALL")
    elseif(_BRI_UPPER STREQUAL "HEADERS_ONLY")
        set(_BAN_RAW_INTS_MODE "HEADERS_ONLY")
    else()
        set(_BAN_RAW_INTS_MODE "OFF")
    endif()
endif()

if(NOT DEFINED KG_ALLOWED_ANGLE_PREFIXES)
    set(KG_ALLOWED_ANGLE_PREFIXES "keel/")
endif()

# Strip C++11 raw string literals `R"delim(...)delim"` in O(N), preserving newline count
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
        string(SUBSTRING "${BODY_AND_REST}" 0 ${REL_CLOSE_POS} RAW_BODY)
        string(REGEX REPLACE "[^\n]+" "" RAW_NLS "${RAW_BODY}")
        string(APPEND ACCUM "${PREFIX_PART}\"\"${RAW_NLS}")

        string(LENGTH "${CLOSER}" CLOSER_LEN)
        math(EXPR REST_OFFSET "${REL_CLOSE_POS} + ${CLOSER_LEN}")
        string(SUBSTRING "${BODY_AND_REST}" ${REST_OFFSET} -1 REM)
    endwhile()

    set(${OUT_VAR} "${ACCUM}" PARENT_SCOPE)
endfunction()

# Strip C++14 numeric digit separators (e.g. `1'000'000`, `0xFF'AA`) before char-literal matching,
# without ever touching char literals (`'a'`, `u8'0'`) or words (`"don't"`).
function(keel_strip_digit_separators INPUT_TEXT OUT_VAR)
    set(TXT "${INPUT_TEXT}")
    string(FIND "${TXT}" "'" HAS_APOS)
    if(NOT HAS_APOS EQUAL -1)
        while(TXT MATCHES "(^|[^A-Za-z0-9_])[0-9][0-9A-Za-z_]*'[0-9A-Za-z_]")
            string(REGEX REPLACE
                "((^|[^A-Za-z0-9_])[0-9][0-9A-Za-z_]*)'([0-9A-Za-z_])"
                "\\1\\3"
                TXT
                "${TXT}"
            )
        endwhile()
    endif()
    set(${OUT_VAR} "${TXT}" PARENT_SCOPE)
endfunction()

# Strip comments, strings, and char literals while preserving exact line numbers (slow-path used
# when a file has multiline block comments and at least one violation to report with file:line).
function(keel_clean_preserve_lines INPUT_TEXT OUT_VAR)
    set(ACCUM "")
    set(REM "${INPUT_TEXT}")
    set(TOKEN_RX "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*|\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(\\\\.[^'\\\\]*)*')")
    while(TRUE)
        string(REGEX MATCH "${TOKEN_RX}" MATCHED "${REM}")
        if(MATCHED STREQUAL "")
            string(APPEND ACCUM "${REM}")
            break()
        endif()
        string(FIND "${REM}" "${MATCHED}" MPOS)
        if(MPOS GREATER 0)
            string(SUBSTRING "${REM}" 0 ${MPOS} HEAD)
            string(APPEND ACCUM "${HEAD}")
        endif()
        string(REGEX REPLACE "[^\n]+" "" MATCH_NLS "${MATCHED}")
        string(APPEND ACCUM " ${MATCH_NLS}")
        string(LENGTH "${MATCHED}" MLEN)
        math(EXPR NEXT_POS "${MPOS} + ${MLEN}")
        string(SUBSTRING "${REM}" ${NEXT_POS} -1 REM)
    endwhile()
    set(${OUT_VAR} "${ACCUM}" PARENT_SCOPE)
endfunction()

# Prepare preprocessed code for scanning: normalize CRLF, strip `// keel-lint: ignore` lines,
# raw strings, digit separators, and UDL boilerplate.
function(keel_preprocess_source RAW_INPUT OUT_NO_RAW OUT_PRE_CLEAN)
    string(REPLACE "\r\n" "\n" NORM "${RAW_INPUT}")
    string(REPLACE "\r" "\n" NORM "${NORM}")

    # 0. Honor per-line `// keel-lint: ignore` escape hatch (preserving line count)
    string(FIND "${NORM}" "keel-lint:" HAS_LINT_PRAGMA)
    if(NOT HAS_LINT_PRAGMA EQUAL -1)
        string(REGEX REPLACE "[^\n]*//[ \t]*keel-lint:[ \t]*ignore[^\n]*" "" NORM "${NORM}")
    endif()

    # 1. Strip raw string literals `R"delim(...)delim"` (preserving newlines)
    string(FIND "${NORM}" "R\"" HAS_RAW_STR)
    if(NOT HAS_RAW_STR EQUAL -1)
        keel_strip_raw_strings("${NORM}" NO_RAW)
    else()
        set(NO_RAW "${NORM}")
    endif()

    # 2. Strip C++14 numeric digit separators (`1'000`) before char-literal stripping
    keel_strip_digit_separators("${NO_RAW}" NO_SEP)

    # 3. Allow ISO C++ standard-mandated UDL signatures (`operator""_foo(unsigned long long)`)
    string(REGEX REPLACE
        "operator[ \t]*\"\"[ \t]*_[A-Za-z0-9_]*[ \t]*\\([ \t]*(unsigned[ \t]+long[ \t]+long|long[ \t]+double)([^)]*)\\)"
        "operator_udl()"
        PRE_CLEAN
        "${NO_SEP}"
    )

    set(${OUT_NO_RAW} "${NO_RAW}" PARENT_SCOPE)
    set(${OUT_PRE_CLEAN} "${PRE_CLEAN}" PARENT_SCOPE)
endfunction()

function(keel_is_allowed_angle_header INC_NAME OUT_ALLOWED)
    if(INC_NAME IN_LIST ALLOWED_HEADER_STD_INCLUDES)
        set(${OUT_ALLOWED} TRUE PARENT_SCOPE)
        return()
    endif()
    foreach(_PFX IN LISTS KG_ALLOWED_ANGLE_PREFIXES)
        if(NOT _PFX STREQUAL "")
            string(FIND "${INC_NAME}" "${_PFX}" _POS)
            if(_POS EQUAL 0)
                set(${OUT_ALLOWED} TRUE PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()
    set(${OUT_ALLOWED} FALSE PARENT_SCOPE)
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
    "cctype"
    "chrono"
    "compare"
    "complex"
    "concepts"
    "cstddef"
    "cstdint"
    "ctime"
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
    "memory_resource"
    "numbers"
    "optional"
    "queue"
    "random"
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
    "string|wstring|u8string|string_view|vector|span|optional|nullopt|array|function|unique_ptr|shared_ptr|weak_ptr|variant|monostate|unordered_map|unordered_set|map|set|source_location|type_index|chrono|complex|pmr|memory_resource|mt19937|mt19937_64|minstd_rand|minstd_rand0|ranlux24|ranlux48|knuth_b|default_random_engine|random_device|seed_seq|shuffle|uniform_int_distribution|uniform_real_distribution|bernoulli_distribution|binomial_distribution|geometric_distribution|negative_binomial_distribution|poisson_distribution|exponential_distribution|gamma_distribution|weibull_distribution|extreme_value_distribution|normal_distribution|lognormal_distribution|chi_squared_distribution|cauchy_distribution|fisher_f_distribution|student_t_distribution|discrete_distribution|piecewise_constant_distribution|piecewise_linear_distribution"
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
    "(^|[^A-Za-z0-9_])(u?int((_fast|_least)?(8|16|32|64)|ptr|max)_t|s?size_t|ptrdiff_t|short|long|unsigned|signed|int)([^A-Za-z0-9_]|$)"
)

# Per-file incremental mtime/size cache (enabled when STAMP_FILE is provided and not running standalone headers)
set(USE_FILE_CACHE FALSE)
set(CACHE_FILE "")
set(CURRENT_CACHE_KEY "")
if(DEFINED STAMP_FILE AND NOT "${STAMP_FILE}" STREQUAL "" AND NOT RUN_STANDALONE_HEADERS)
    set(USE_FILE_CACHE TRUE)
    set(CACHE_FILE "${STAMP_FILE}.cache.cmake")
    file(SHA256 "${CONFIG_FILE}" _CFG_SHA)
    file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _SCR_SHA)
    set(CURRENT_CACHE_KEY "${_CFG_SHA}_${_SCR_SHA}")
    if(EXISTS "${CACHE_FILE}")
        include("${CACHE_FILE}")
    endif()
    if(NOT DEFINED KG_CACHE_KEY OR NOT KG_CACHE_KEY STREQUAL "${CURRENT_CACHE_KEY}")
        set(CACHE_VALID FALSE)
    else()
        set(CACHE_VALID TRUE)
    endif()
endif()

set(NEW_CACHE_LINES "# Auto-generated keel guidelines per-file cache\nset(KG_CACHE_KEY \"${CURRENT_CACHE_KEY}\")\n")
set(ALL_VIOLATIONS "")
set(CHECKED_COUNT 0)
set(CACHED_COUNT 0)

# Collect all violations inside a single file with exact 1-based line numbers
function(keel_collect_file_violations REL_FILE IS_HEADER CHECK_RAW_INTS NO_RAW_CONTENT PRE_CLEAN OUT_VIOLATIONS)
    set(_FILE_VIOLS "")

    # Preserve exact line numbers even if multiline /* ... */ comments exist
    string(FIND "${PRE_CLEAN}" "/*" HAS_BLOCK_CMT)
    if(NOT HAS_BLOCK_CMT EQUAL -1)
        keel_clean_preserve_lines("${PRE_CLEAN}" LINE_CLEAN)
    else()
        string(REGEX REPLACE
            "(//[^\n]*|\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(\\\\.[^'\\\\]*)*')"
            " "
            LINE_CLEAN
            "${PRE_CLEAN}"
        )
    endif()

    if(KG_FORBID_HEADERS)
        string(FIND "${NO_RAW_CONTENT}" "/*" HAS_RAW_BLOCK_CMT)
        if(NOT HAS_RAW_BLOCK_CMT EQUAL -1)
            set(_INC_SCAN "${NO_RAW_CONTENT}")
            # Strip comments while preserving newlines, keeping quoted include paths intact
            set(_INC_ACCUM "")
            set(_INC_REM "${_INC_SCAN}")
            while(TRUE)
                string(REGEX MATCH "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*)" _CMT "${_INC_REM}")
                if(_CMT STREQUAL "")
                    string(APPEND _INC_ACCUM "${_INC_REM}")
                    break()
                endif()
                string(FIND "${_INC_REM}" "${_CMT}" _CPOS)
                if(_CPOS GREATER 0)
                    string(SUBSTRING "${_INC_REM}" 0 ${_CPOS} _CHEAD)
                    string(APPEND _INC_ACCUM "${_CHEAD}")
                endif()
                string(REGEX REPLACE "[^\n]+" "" _CNLS "${_CMT}")
                string(APPEND _INC_ACCUM " ${_CNLS}")
                string(LENGTH "${_CMT}" _CLEN)
                math(EXPR _CNEXT "${_CPOS} + ${_CLEN}")
                string(SUBSTRING "${_INC_REM}" ${_CNEXT} -1 _INC_REM)
            endwhile()
            set(NO_CMT_INC "${_INC_ACCUM}")
        else()
            string(REGEX REPLACE "//[^\n]*" " " NO_CMT_INC "${NO_RAW_CONTENT}")
        endif()

        string(REPLACE ";" "__KEEL_SEMI__" _INC_SAFE "${NO_CMT_INC}")
        string(REPLACE "\n" ";@" _INC_LINES "@${_INC_SAFE}")
        set(_LNO 0)
        foreach(_RL IN LISTS _INC_LINES)
            math(EXPR _LNO "${_LNO} + 1")
            string(SUBSTRING "${_RL}" 1 -1 _LINE)
            if(_LINE MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
                set(_INC_NAME "${CMAKE_MATCH_1}")
                if(_INC_NAME IN_LIST KG_FORBID_HEADERS AND NOT _INC_NAME IN_LIST KG_ALLOW_HEADERS)
                    list(APPEND _FILE_VIOLS
                        "${REL_FILE}:${_LNO}: forbidden '#include <${_INC_NAME}>'. Use high-level ecosystem abstractions or add to ALLOW_HEADERS."
                    )
                endif()
            endif()
        endforeach()
    endif()

    if(IS_HEADER)
        set(_ACTIVE_STD_RX "${FORBIDDEN_STD_REGEX_HDR}")
    else()
        set(_ACTIVE_STD_RX "${FORBIDDEN_STD_REGEX_CPP}")
    endif()

    # Split LINE_CLEAN into exact 1-based lines without collapsing empty lines or semicolons
    string(REPLACE ";" "__KEEL_SEMI__" _SAFE_CLEAN "${LINE_CLEAN}")
    string(REPLACE "\n" ";@" _CODE_LINES "@${_SAFE_CLEAN}")
    set(_LNO 0)
    foreach(_RL IN LISTS _CODE_LINES)
        math(EXPR _LNO "${_LNO} + 1")
        string(SUBSTRING "${_RL}" 1 -1 _LINE)
        string(REPLACE "__KEEL_SEMI__" ";" _LINE "${_LINE}")

        if(KG_BAN_STL)
            if(_LINE MATCHES "^[ \t]*#[ \t]*include[ \t]*<([^>]+)>")
                set(_INC_NAME "${CMAKE_MATCH_1}")
                if(IS_HEADER)
                    keel_is_allowed_angle_header("${_INC_NAME}" _IS_ALLOWED_INC)
                    if(NOT _IS_ALLOWED_INC)
                        if(_INC_NAME STREQUAL "cstdint" OR _INC_NAME STREQUAL "cstddef")
                            list(APPEND _FILE_VIOLS
                                "${REL_FILE}:${_LNO}: forbidden '#include <${_INC_NAME}>'. Include \"keel/core.h\" and use keel::i8..i64 / keel::u8..u64 / keel::isize / keel::usize."
                            )
                        elseif(_INC_NAME STREQUAL "stdexcept")
                            list(APPEND _FILE_VIOLS
                                "${REL_FILE}:${_LNO}: '#include <stdexcept>' pulls in <string>. Use <exception> or \"keel/exception.h\" (keel::Exception) in headers."
                            )
                        else()
                            list(APPEND _FILE_VIOLS
                                "${REL_FILE}:${_LNO}: forbidden '#include <${_INC_NAME}>'. Allowed angle-bracket includes in headers: <cassert>, <cmath>, <cstring>, <exception>, <initializer_list>, <new>, or ALLOWED_ANGLE_PREFIXES."
                            )
                        endif()
                    endif()
                else()
                    if(_INC_NAME IN_LIST FORBIDDEN_CPP_STD_INCLUDES)
                        if(_INC_NAME STREQUAL "cstdint" OR _INC_NAME STREQUAL "cstddef")
                            list(APPEND _FILE_VIOLS
                                "${REL_FILE}:${_LNO}: forbidden '#include <${_INC_NAME}>'. Include \"keel/core.h\" and use keel::i8..i64 / keel::u8..u64 / keel::isize / keel::usize."
                            )
                        else()
                            list(APPEND _FILE_VIOLS
                                "${REL_FILE}:${_LNO}: forbidden STL header '#include <${_INC_NAME}>'. Use keel equivalents (\"keel/array.h\", \"keel/string.h\", \"keel/hash_map.h\", \"keel/math.h\", \"keel/random.h\", \"keel/exception.h\", etc.)."
                            )
                        endif()
                    endif()
                endif()
            endif()

            if(_LINE MATCHES "${_ACTIVE_STD_RX}")
                string(REGEX MATCH "${_ACTIVE_STD_RX}" _MSTD "${_LINE}")
                string(STRIP "${_MSTD}" _MSTD)
                list(APPEND _FILE_VIOLS
                    "${REL_FILE}:${_LNO}: forbidden STL symbol '${_MSTD}' (use keel::* instead)."
                )
            endif()
        endif()

        if(IS_HEADER AND _LINE MATCHES "(^|[^A-Za-z0-9_])using[ \t]+namespace[ \t]+")
            list(APPEND _FILE_VIOLS
                "${REL_FILE}:${_LNO}: 'using namespace' is forbidden in headers."
            )
        endif()

        if(CHECK_RAW_INTS)
            set(_INT_LINE "${_LINE}")
            string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*[,\\)])" "\\1keel_main(keel_argc," _INT_LINE "${_INT_LINE}")
            string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\()" "\\1keel_main(" _INT_LINE "${_INT_LINE}")
            string(REGEX REPLACE "operator[ \t]*(\\+\\+|--)[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*\\)" "operator_postinc()" _INT_LINE "${_INT_LINE}")
            if(_INT_LINE MATCHES "${FORBIDDEN_RAW_INT_REGEX}")
                string(REGEX MATCH "${FORBIDDEN_RAW_INT_REGEX}" _MINT "${_INT_LINE}")
                string(STRIP "${_MINT}" _MINT)
                list(APPEND _FILE_VIOLS
                    "${REL_FILE}:${_LNO}: forbidden raw integer token '${_MINT}'. Use keel::i8..i64, keel::u8..u64, keel::isize, or keel::usize (annotate true C/OS ABI boundary lines with '// keel-lint: ignore')."
                )
            endif()
        endif()
    endforeach()

    set(${OUT_VIOLATIONS} "${_FILE_VIOLS}" PARENT_SCOPE)
endfunction()

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

    set(CHECK_RAW_INTS FALSE)
    if(_BAN_RAW_INTS_MODE STREQUAL "ALL" OR (_BAN_RAW_INTS_MODE STREQUAL "HEADERS_ONLY" AND IS_HEADER))
        set(CHECK_RAW_INTS TRUE)
    endif()

    # Check per-file incremental mtime/size cache
    set(FILE_ID "")
    set(FILE_SIG "")
    if(USE_FILE_CACHE)
        string(MD5 FILE_ID "${FILE_PATH}")
        file(TIMESTAMP "${FILE_PATH}" _F_MTIME "%s" UTC)
        file(SIZE "${FILE_PATH}" _F_SIZE)
        set(FILE_SIG "${_F_MTIME}_${_F_SIZE}")
        if(CACHE_VALID AND DEFINED KG_CACHED_${FILE_ID} AND KG_CACHED_${FILE_ID} STREQUAL "${FILE_SIG}")
            math(EXPR CACHED_COUNT "${CACHED_COUNT} + 1")
            string(APPEND NEW_CACHE_LINES "set(KG_CACHED_${FILE_ID} \"${FILE_SIG}\")\n")
            continue()
        endif()
    endif()

    math(EXPR CHECKED_COUNT "${CHECKED_COUNT} + 1")

    file(RELATIVE_PATH REL_FILE "${KG_SOURCE_ROOT}" "${FILE_PATH}")
    if(REL_FILE MATCHES "^\\.\\.")
        set(REL_FILE "${FILE_PATH}")
    endif()

    file(READ "${FILE_PATH}" RAW_CONTENT)
    keel_preprocess_source("${RAW_CONTENT}" NO_RAW_CONTENT PRE_CLEAN)

    # Fast-path whole-file clean & check (single-pass unrolled-loop regex)
    string(REGEX REPLACE
        "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*|\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"|'[^'\\\\]*(\\\\.[^'\\\\]*)*')"
        " "
        CLEAN_CODE
        "${PRE_CLEAN}"
    )

    set(FILE_HAS_VIOLATION FALSE)

    if(KG_FORBID_HEADERS)
        string(REGEX REPLACE "(/\\*[^*]*\\*+([^/*][^*]*\\*+)*/|//[^\n]*)" " " NO_COMMENTS_FOR_INC "${NO_RAW_CONTENT}")
        string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" ALL_INC_LINES "${NO_COMMENTS_FOR_INC}")
        foreach(INC_LINE IN LISTS ALL_INC_LINES)
            string(REGEX REPLACE "^.*[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" INC_NAME "${INC_LINE}")
            if(INC_NAME IN_LIST KG_FORBID_HEADERS AND NOT INC_NAME IN_LIST KG_ALLOW_HEADERS)
                set(FILE_HAS_VIOLATION TRUE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT FILE_HAS_VIOLATION AND KG_BAN_STL)
        string(REGEX MATCHALL "(^|\n)[ \t]*#[ \t]*include[ \t]*<[^>]+>" ANGLE_INC_LINES "${CLEAN_CODE}")
        foreach(INC_LINE IN LISTS ANGLE_INC_LINES)
            string(REGEX REPLACE "^.*[ \t]*#[ \t]*include[ \t]*<([^>]+)>.*$" "\\1" INC_NAME "${INC_LINE}")
            if(IS_HEADER)
                keel_is_allowed_angle_header("${INC_NAME}" IS_ALLOWED_INC)
                if(NOT IS_ALLOWED_INC)
                    set(FILE_HAS_VIOLATION TRUE)
                    break()
                endif()
            else()
                if(INC_NAME IN_LIST FORBIDDEN_CPP_STD_INCLUDES)
                    set(FILE_HAS_VIOLATION TRUE)
                    break()
                endif()
            endif()
        endforeach()
    endif()

    if(NOT FILE_HAS_VIOLATION AND IS_HEADER)
        if(CLEAN_CODE MATCHES "(^|[^A-Za-z0-9_])using[ \t]+namespace[ \t]+")
            set(FILE_HAS_VIOLATION TRUE)
        endif()
    endif()

    if(NOT FILE_HAS_VIOLATION AND KG_BAN_STL)
        if(IS_HEADER)
            set(ACTIVE_STD_REGEX "${FORBIDDEN_STD_REGEX_HDR}")
        else()
            set(ACTIVE_STD_REGEX "${FORBIDDEN_STD_REGEX_CPP}")
        endif()
        if(CLEAN_CODE MATCHES "${ACTIVE_STD_REGEX}")
            set(FILE_HAS_VIOLATION TRUE)
        endif()
    endif()

    if(NOT FILE_HAS_VIOLATION AND CHECK_RAW_INTS)
        set(INT_CHECK_CODE "${CLEAN_CODE}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*[,\\)])" "\\1keel_main(keel_argc," INT_CHECK_CODE "${INT_CHECK_CODE}")
        string(REGEX REPLACE "(^|[^A-Za-z0-9_])int([ \t]+main[ \t]*\\()" "\\1keel_main(" INT_CHECK_CODE "${INT_CHECK_CODE}")
        string(REGEX REPLACE "operator[ \t]*(\\+\\+|--)[ \t]*\\([ \t]*int([ \t]+[A-Za-z0-9_]*)?[ \t]*\\)" "operator_postinc()" INT_CHECK_CODE "${INT_CHECK_CODE}")
        if(INT_CHECK_CODE MATCHES "${FORBIDDEN_RAW_INT_REGEX}")
            set(FILE_HAS_VIOLATION TRUE)
        endif()
    endif()

    if(FILE_HAS_VIOLATION)
        keel_collect_file_violations("${REL_FILE}" ${IS_HEADER} ${CHECK_RAW_INTS} "${NO_RAW_CONTENT}" "${PRE_CLEAN}" FILE_VIOLS)
        if(FILE_VIOLS)
            list(APPEND ALL_VIOLATIONS ${FILE_VIOLS})
        else()
            list(APPEND ALL_VIOLATIONS "${REL_FILE}:1: guideline violation detected.")
        endif()
    elseif(USE_FILE_CACHE)
        string(APPEND NEW_CACHE_LINES "set(KG_CACHED_${FILE_ID} \"${FILE_SIG}\")\n")
    endif()
endforeach()

# Persist cache for all clean files (so subsequent runs only re-scan modified or failing files)
if(USE_FILE_CACHE)
    get_filename_component(_CACHE_DIR "${CACHE_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_CACHE_DIR}")
    file(WRITE "${CACHE_FILE}" "${NEW_CACHE_LINES}")
endif()

list(LENGTH ALL_VIOLATIONS VIOLATION_COUNT)
if(VIOLATION_COUNT GREATER 0)
    set(REPORT "")
    foreach(V IN LISTS ALL_VIOLATIONS)
        string(APPEND REPORT "  ${V}\n")
    endforeach()
    message(FATAL_ERROR
        "[keel-guidelines] Target '${KG_TARGET}' failed with ${VIOLATION_COUNT} violation(s):\n${REPORT}"
    )
endif()

if(DEFINED KEEL_GUIDELINES_VERBOSE AND KEEL_GUIDELINES_VERBOSE)
    message(STATUS "[keel-guidelines] '${KG_TARGET}': ok (checked=${CHECKED_COUNT}, cached=${CACHED_COUNT}).")
endif()

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

    set(COMPILE_DEF_FLAGS "")
    if(DEFINED KG_COMPILE_DEFS AND KG_COMPILE_DEFS)
        foreach(CDEF IN LISTS KG_COMPILE_DEFS)
            if(NOT CDEF STREQUAL "")
                if(KG_COMPILER_ID MATCHES "MSVC")
                    list(APPEND COMPILE_DEF_FLAGS "/D${CDEF}")
                else()
                    list(APPEND COMPILE_DEF_FLAGS "-D${CDEF}")
                endif()
            endif()
        endforeach()
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
            COMMAND "${KG_COMPILER_PATH}" /nologo /std:c++20 /Zs /showIncludes ${SYS_INC_FLAGS} ${COMPILE_DEF_FLAGS} "${BASELINE_SRC}"
            OUTPUT_VARIABLE BASELINE_OUT
            ERROR_VARIABLE BASELINE_ERR
            RESULT_VARIABLE BASELINE_RES
        )
    else()
        execute_process(
            COMMAND "${KG_COMPILER_PATH}" -std=c++20 -fsyntax-only -H ${SYS_INC_FLAGS} ${COMPILE_DEF_FLAGS} "${BASELINE_SRC}"
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
            if(NOT NORM_ROOT MATCHES "/$")
                string(APPEND NORM_ROOT "/")
            endif()
            list(APPEND NORM_ALLOWED_ROOTS "${NORM_ROOT}")
        endif()
    endforeach()

    set(STANDALONE_COUNT 0)
    set(STANDALONE_ERRORS "")
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
                    COMMAND "${KG_COMPILER_PATH}" /nologo /std:c++20 /Zs /showIncludes ${SYS_INC_FLAGS} ${INC_ROOT_FLAGS} ${COMPILE_DEF_FLAGS} "${TEST_SRC}"
                    OUTPUT_VARIABLE HDR_OUT
                    ERROR_VARIABLE HDR_ERR
                    RESULT_VARIABLE HDR_RES
                )
            else()
                execute_process(
                    COMMAND "${KG_COMPILER_PATH}" -std=c++20 -fsyntax-only -H ${SYS_INC_FLAGS} ${INC_ROOT_FLAGS} ${COMPILE_DEF_FLAGS} "${TEST_SRC}"
                    OUTPUT_VARIABLE HDR_OUT
                    ERROR_VARIABLE HDR_ERR
                    RESULT_VARIABLE HDR_RES
                )
            endif()

            set(COMBINED_HDR "${HDR_OUT}\n${HDR_ERR}")
            if(NOT HDR_RES EQUAL 0)
                list(APPEND STANDALONE_ERRORS "Standalone compilation failed for ${HDR_REL}:\n${COMBINED_HDR}")
                continue()
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
                        list(APPEND STANDALONE_ERRORS
                            "Transitive header purity violation in ${HDR_REL}: unexpected system header '${EXTRACTED}'"
                        )
                    endif()
                endif()
            endforeach()

            math(EXPR STANDALONE_COUNT "${STANDALONE_COUNT} + 1")
        endforeach()
    endforeach()

    list(LENGTH STANDALONE_ERRORS STANDALONE_ERR_COUNT)
    if(STANDALONE_ERR_COUNT GREATER 0)
        set(SREPORT "")
        foreach(SE IN LISTS STANDALONE_ERRORS)
            string(APPEND SREPORT "  ${SE}\n")
        endforeach()
        message(FATAL_ERROR
            "[keel-guidelines] Standalone header purity failed for '${KG_TARGET}' (${STANDALONE_ERR_COUNT} error(s)):\n${SREPORT}"
        )
    endif()

    message(STATUS "[keel-guidelines] Standalone header purity passed for '${KG_TARGET}' (${STANDALONE_COUNT} headers).")
endif()

if(DEFINED STAMP_FILE AND NOT "${STAMP_FILE}" STREQUAL "")
    get_filename_component(STAMP_DIR "${STAMP_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${STAMP_DIR}")
    file(WRITE "${STAMP_FILE}" "ok\n")
endif()
