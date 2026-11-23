# Self-test for KeelGuidelines.cmake / KeelCheckGuidelines.cmake
# Exercises:
#   1. Clean code with C++14 digit separators, char/raw literals, block comments, UDL, main,
#      `// keel-lint: ignore`, and `#include <keel/...>` / `ALLOWED_ANGLE_PREFIXES`.
#   2. Multi-violation reporting with exact 1-based line numbers across empty lines,
#      multiline block comments, and odd digit separators (`1'000`).
#   3. `BAN_RAW_INTS HEADERS_ONLY` mode.
#   4. Per-file mtime/size incremental cache hit & invalidation.
#   5. `STANDALONE_HEADERS ON` with `KG_COMPILE_DEFS` and trailing-slash root boundary check.

if(NOT DEFINED CHECK_SCRIPT OR NOT EXISTS "${CHECK_SCRIPT}")
    message(FATAL_ERROR "CHECK_SCRIPT must point to KeelCheckGuidelines.cmake")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/src" "${WORK_DIR}/include/myproj")

# ---------------------------------------------------------------------------
# Case 1: Clean file with all tricky syntax + angle-bracket includes
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/include/myproj/clean.hpp"
"#pragma once
#include <keel/core.h>
#include <myproj/helper.hpp>

struct Counter {
    keel::u64 v = 1'000'000;
    keel::u64 h = 0xDEAD'BEEF;
    keel::u32 b = 0b1010'0101;
    char q = '\\'';
    char8_t c8 = u8'0';
    Counter operator++(int) noexcept { Counter c = *this; ++v; return c; }
};

constexpr keel::u64 operator\"\"_kb(unsigned long long n) noexcept { return n * 1024ULL; }
")

file(WRITE "${WORK_DIR}/include/myproj/helper.hpp"
"#pragma once
#include <keel/core.h>
#ifndef KEEL_TEST_REQUIRED_DEF
#error KEEL_TEST_REQUIRED_DEF must be propagated to STANDALONE_HEADERS
#endif
inline keel::usize helper_val() noexcept { return 1'000; }
")

file(WRITE "${WORK_DIR}/src/clean.cpp"
"#include <myproj/clean.hpp>
/* multiline comment with forbidden words:
   int size_t std::vector std::mt19937 #include <random>
*/
const char* raw = R\"delim(
int not_code = 1;
size_t also_not_code = 2;
std::vector<int> v;
)delim\";

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    keel::u64 odd_sep = 1'000;
    int abi_fd = 0; // keel-lint: ignore
    (void)odd_sep; (void)abi_fd;
    return 0;
}
")

file(WRITE "${WORK_DIR}/cfg_clean.cmake"
"set(KG_TARGET \"test_clean\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_COMPILER_PATH \"${COMPILER_PATH}\")
set(KG_COMPILER_ID \"${COMPILER_ID}\")
set(KG_STD_INC_DIRS \"${STD_INC_DIRS}\")
set(KG_ALLOW_HEADERS \"\")
set(KG_FORBID_HEADERS \"\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_COMPILE_DEFS \"KEEL_TEST_REQUIRED_DEF=1\")
set(KG_EXCLUDE_FILES \"\")
set(KG_HEADERS_DIRS \"${WORK_DIR}/include\")
set(KG_ALLOWED_INCLUDE_ROOTS \"${WORK_DIR}/include;${KEEL_INCLUDE_DIR}\")
set(KG_FILES
    \"${WORK_DIR}/include/myproj/clean.hpp\"
    \"${WORK_DIR}/include/myproj/helper.hpp\"
    \"${WORK_DIR}/src/clean.cpp\"
)
")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${WORK_DIR}/cfg_clean.cmake"
        "-DRUN_STANDALONE_HEADERS=OFF"
        "-DSTAMP_FILE=${WORK_DIR}/clean.stamp"
        "-DKEEL_GUIDELINES_VERBOSE=ON"
        -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT1
    ERROR_VARIABLE ERR1
    RESULT_VARIABLE RES1
)
if(NOT RES1 EQUAL 0)
    message(FATAL_ERROR "Case 1 (clean) failed unexpectedly:\n${OUT1}\n${ERR1}")
endif()
if(NOT "${OUT1}" MATCHES "checked=3, cached=0")
    message(FATAL_ERROR "Case 1 expected 'checked=3, cached=0', got:\n${OUT1}")
endif()

# ---------------------------------------------------------------------------
# Case 2: Incremental per-file cache (2nd run hits cache, 3rd re-checks 1 file)
# ---------------------------------------------------------------------------
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${WORK_DIR}/cfg_clean.cmake"
        "-DRUN_STANDALONE_HEADERS=OFF"
        "-DSTAMP_FILE=${WORK_DIR}/clean.stamp"
        "-DKEEL_GUIDELINES_VERBOSE=ON"
        -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT2
    ERROR_VARIABLE ERR2
    RESULT_VARIABLE RES2
)
if(NOT RES2 EQUAL 0 OR NOT "${OUT2}" MATCHES "checked=0, cached=3")
    message(FATAL_ERROR "Case 2a (full cache hit) failed:\n${OUT2}\n${ERR2}")
endif()

file(APPEND "${WORK_DIR}/src/clean.cpp" "\n// appended comment changing size\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${WORK_DIR}/cfg_clean.cmake"
        "-DRUN_STANDALONE_HEADERS=OFF"
        "-DSTAMP_FILE=${WORK_DIR}/clean.stamp"
        "-DKEEL_GUIDELINES_VERBOSE=ON"
        -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT3
    ERROR_VARIABLE ERR3
    RESULT_VARIABLE RES3
)
if(NOT RES3 EQUAL 0 OR NOT "${OUT3}" MATCHES "checked=1, cached=2")
    message(FATAL_ERROR "Case 2b (partial cache hit) failed:\n${OUT3}\n${ERR3}")
endif()

# ---------------------------------------------------------------------------
# Case 3: Multi-violation + exact line numbers across multiline comment,
#         empty line, and odd digit separator `1'000`
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/bad.cpp"
"#include <random>
/* multiline
   block comment */
keel::u64 n = 1'000;
std::mt19937 rng;

size_t bad_sz = 0;
int also_bad = 1;
")

file(WRITE "${WORK_DIR}/cfg_bad.cmake"
"set(KG_TARGET \"test_bad\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/\")
set(KG_FILES \"${WORK_DIR}/src/bad.cpp\")
")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${WORK_DIR}/cfg_bad.cmake"
        "-DRUN_STANDALONE_HEADERS=OFF"
        -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_BAD
    ERROR_VARIABLE ERR_BAD
    RESULT_VARIABLE RES_BAD
)
set(COMB_BAD "${OUT_BAD}\n${ERR_BAD}")
if(RES_BAD EQUAL 0)
    message(FATAL_ERROR "Case 3 (bad.cpp) unexpectedly passed!")
endif()
foreach(NEED IN ITEMS "src/bad.cpp:1:" "src/bad.cpp:5:" "src/bad.cpp:7:" "src/bad.cpp:8:")
    string(FIND "${COMB_BAD}" "${NEED}" FPOS)
    if(FPOS EQUAL -1)
        message(FATAL_ERROR "Case 3 missing expected diagnostic '${NEED}' in output:\n${COMB_BAD}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Case 4: BAN_RAW_INTS HEADERS_ONLY allows `int` in .cpp but rejects in .hpp
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/cpp_int.cpp"
"int legacy_cpp_fn() { return 0; }
")
file(WRITE "${WORK_DIR}/include/myproj/hdr_int.hpp"
"#pragma once
int bad_header_fn();
")

file(WRITE "${WORK_DIR}/cfg_hdr_only_pass.cmake"
"set(KG_TARGET \"test_hdr_only_pass\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"HEADERS_ONLY\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES
    \"${WORK_DIR}/include/myproj/clean.hpp\"
    \"${WORK_DIR}/src/cpp_int.cpp\"
)
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_hdr_only_pass.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_H1 ERROR_VARIABLE ERR_H1 RESULT_VARIABLE RES_H1
)
if(NOT RES_H1 EQUAL 0)
    message(FATAL_ERROR "Case 4a (HEADERS_ONLY pass) failed:\n${OUT_H1}\n${ERR_H1}")
endif()

file(WRITE "${WORK_DIR}/cfg_hdr_only_fail.cmake"
"set(KG_TARGET \"test_hdr_only_fail\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"HEADERS_ONLY\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES \"${WORK_DIR}/include/myproj/hdr_int.hpp\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_hdr_only_fail.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_H2 ERROR_VARIABLE ERR_H2 RESULT_VARIABLE RES_H2
)
if(RES_H2 EQUAL 0 OR NOT "${OUT_H2}\n${ERR_H2}" MATCHES "include/myproj/hdr_int.hpp:2:")
    message(FATAL_ERROR "Case 4b (HEADERS_ONLY fail) did not report hdr_int.hpp:2:\n${OUT_H2}\n${ERR_H2}")
endif()

# ---------------------------------------------------------------------------
# Case 5: STANDALONE_HEADERS ON with COMPILE_DEFINITIONS
# ---------------------------------------------------------------------------
file(REMOVE "${WORK_DIR}/include/myproj/hdr_int.hpp")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${WORK_DIR}/cfg_clean.cmake"
        "-DRUN_STANDALONE_HEADERS=ON"
        -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_SA
    ERROR_VARIABLE ERR_SA
    RESULT_VARIABLE RES_SA
)
if(NOT RES_SA EQUAL 0)
    message(FATAL_ERROR "Case 5 (STANDALONE_HEADERS with COMPILE_DEFS) failed:\n${OUT_SA}\n${ERR_SA}")
endif()

# ---------------------------------------------------------------------------
# Case 6: BAN_RAW_FLOATS rejects float/double, allows keel::f32/f64 and keel-lint: ignore
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/bad_float.cpp"
"#include <keel/core.h>
double bad_val = 1.0;
float bad_f = 2.0f;
keel::f64 ok_val = 3.0;
double ignored_val = 4.0; // keel-lint: ignore
")
file(WRITE "${WORK_DIR}/cfg_bad_float.cmake"
"set(KG_TARGET \"test_bad_float\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_BAN_RAW_FLOATS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES \"${WORK_DIR}/src/bad_float.cpp\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_bad_float.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_BF ERROR_VARIABLE ERR_BF RESULT_VARIABLE RES_BF
)
set(COMBINED_BF "${OUT_BF}\n${ERR_BF}")
if(RES_BF EQUAL 0 OR NOT COMBINED_BF MATCHES "src/bad_float.cpp:2:" OR NOT COMBINED_BF MATCHES "src/bad_float.cpp:3:" OR COMBINED_BF MATCHES "src/bad_float.cpp:5:")
    message(FATAL_ERROR "Case 6 (BAN_RAW_FLOATS) failed:\n${COMBINED_BF}")
endif()

# ---------------------------------------------------------------------------
# Case 7: Non-standard math macros (_USE_MATH_DEFINES / M_PI) rejected
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/bad_math_macro.cpp"
"#define _USE_MATH_DEFINES
#include <keel/math.h>
keel::f64 a = M_PI;
keel::f64 b = keel::kPi;
")
file(WRITE "${WORK_DIR}/cfg_bad_math.cmake"
"set(KG_TARGET \"test_bad_math\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_BAN_RAW_FLOATS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES \"${WORK_DIR}/src/bad_math_macro.cpp\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_bad_math.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_BM ERROR_VARIABLE ERR_BM RESULT_VARIABLE RES_BM
)
set(COMBINED_BM "${OUT_BM}\n${ERR_BM}")
if(RES_BM EQUAL 0 OR NOT COMBINED_BM MATCHES "src/bad_math_macro.cpp:1:" OR NOT COMBINED_BM MATCHES "src/bad_math_macro.cpp:3:" OR COMBINED_BM MATCHES "src/bad_math_macro.cpp:4:")
    message(FATAL_ERROR "Case 7 (math macros) failed:\n${COMBINED_BM}")
endif()

# ---------------------------------------------------------------------------
# Case 8: Strict keel-only math / memcpy / limits / ~u64{0} / INFINITY / NAN
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/bad_strict_keel.cpp"
"#include <cmath>
#include <keel/math.h>
#include <keel/memory.h>
void f(keel::f64* dst, const keel::f64* src) {
    keel::f64 a = std::floor(src[0]);
    std::memcpy(dst, src, sizeof(keel::f64));
    keel::u64 m1 = std::numeric_limits<keel::u64>::max();
    keel::u64 m2 = ~keel::u64{0};
    keel::f64 inf = INFINITY;
    keel::f64 ok = keel::floor(src[0]) + keel::kInf;
    keel::u64 okm = keel::Limits<keel::u64>::kMax;
    (void)a; (void)m1; (void)m2; (void)inf; (void)ok; (void)okm;
}
")
file(WRITE "${WORK_DIR}/cfg_bad_strict.cmake"
"set(KG_TARGET \"test_bad_strict\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_BAN_RAW_FLOATS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES \"${WORK_DIR}/src/bad_strict_keel.cpp\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_bad_strict.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_BS ERROR_VARIABLE ERR_BS RESULT_VARIABLE RES_BS
)
set(COMBINED_BS "${OUT_BS}\n${ERR_BS}")
if(RES_BS EQUAL 0
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:1:"
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:5:"
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:6:"
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:7:"
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:8:"
    OR NOT COMBINED_BS MATCHES "src/bad_strict_keel.cpp:9:"
    OR COMBINED_BS MATCHES "src/bad_strict_keel.cpp:10:"
    OR COMBINED_BS MATCHES "src/bad_strict_keel.cpp:11:")
    message(FATAL_ERROR "Case 8 (strict keel rules) failed:\n${COMBINED_BS}")
endif()

# ---------------------------------------------------------------------------
# Case 9: Concurrency types must use keel::Atomic / keel::Thread / keel::Mutex
# ---------------------------------------------------------------------------
file(WRITE "${WORK_DIR}/src/bad_concurrency.cpp"
"#include <atomic>
#include <thread>
#include <mutex>
#include <keel/atomic.h>
#include <keel/sync.h>
#include <keel/thread.h>
void g() {
    std::atomic<keel::i32> a{0};
    std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    std::thread t;
    keel::Atomic<keel::i32> ka{0};
    keel::Mutex km;
    keel::LockGuard<keel::Mutex> klk(km);
    keel::Thread kt;
    (void)a; (void)lk; (void)t; (void)ka; (void)klk; (void)kt;
}
")
file(WRITE "${WORK_DIR}/cfg_bad_conc.cmake"
"set(KG_TARGET \"test_bad_conc\")
set(KG_SCOPE \"ALL\")
set(KG_BAN_STL ON)
set(KG_BAN_RAW_INTS \"ALL\")
set(KG_BAN_RAW_FLOATS \"ALL\")
set(KG_ALLOW_STDEXCEPT ON)
set(KG_ALLOW_STDEXCEPT_IN_HEADERS OFF)
set(KG_SOURCE_ROOT \"${WORK_DIR}\")
set(KG_BINARY_DIR \"${WORK_DIR}/bin\")
set(KG_ALLOWED_ANGLE_PREFIXES \"keel/;myproj/\")
set(KG_FILES \"${WORK_DIR}/src/bad_concurrency.cpp\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCONFIG_FILE=${WORK_DIR}/cfg_bad_conc.cmake" "-DRUN_STANDALONE_HEADERS=OFF" -P "${CHECK_SCRIPT}"
    OUTPUT_VARIABLE OUT_BC ERROR_VARIABLE ERR_BC RESULT_VARIABLE RES_BC
)
set(COMBINED_BC "${OUT_BC}\n${ERR_BC}")
if(RES_BC EQUAL 0
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:1:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:2:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:3:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:8:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:9:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:10:"
    OR NOT COMBINED_BC MATCHES "src/bad_concurrency.cpp:11:"
    OR COMBINED_BC MATCHES "src/bad_concurrency.cpp:12:"
    OR COMBINED_BC MATCHES "src/bad_concurrency.cpp:13:"
    OR COMBINED_BC MATCHES "src/bad_concurrency.cpp:14:"
    OR COMBINED_BC MATCHES "src/bad_concurrency.cpp:15:")
    message(FATAL_ERROR "Case 9 (keel concurrency rules) failed:\n${COMBINED_BC}")
endif()

message(STATUS "All KeelGuidelines self-tests passed.")
