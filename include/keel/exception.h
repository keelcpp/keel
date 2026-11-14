#pragma once

#include <exception>
#include "keel/error.h"
#include "keel/format.h"

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_CPPUNWIND) && _CPPUNWIND != 0)
#define KEEL_HAS_EXCEPTIONS 1
#else
#define KEEL_HAS_EXCEPTIONS 0
#endif

#if KEEL_HAS_EXCEPTIONS

namespace keel {

// Zero-heap-allocation, trivially-copyable-payload exception type compatible with `catch (const std::exception&)`.
// Avoids `<stdexcept>` (which transitively pulls in `<string>`) and guarantees `noexcept` copy during stack unwinding.
class Exception : public std::exception {
public:
    static constexpr usize kBufferSize = 240;
    static constexpr usize kMaxMessageBytes = kBufferSize - 1;

    Exception() noexcept : m_err(ErrorCode::Unknown, 0), m_msg("keel::Exception") {}

    explicit Exception(StringRef msg, Error err = Error{ErrorCode::Unknown, 0}) noexcept
        : m_err(err), m_msg(msg) {}

    explicit Exception(const char* msg, Error err = Error{ErrorCode::Unknown, 0}) noexcept
        : m_err(err), m_msg(msg ? StringRef(msg) : StringRef("keel::Exception")) {}

    explicit Exception(Error err) noexcept
        : m_err(err), m_msg(error_message(err)) {}

    explicit Exception(ErrorCode code, StringRef msg = {}) noexcept
        : m_err(code, 0), m_msg(msg.empty() ? error_message(code) : msg) {}

    Exception(const Exception&) noexcept = default;
    Exception(Exception&&) noexcept = default;
    Exception& operator=(const Exception&) noexcept = default;
    Exception& operator=(Exception&&) noexcept = default;
    ~Exception() noexcept override = default;

    template <typename... Args>
    [[nodiscard]] static Exception format(StringRef fmt, const Args&... args) noexcept {
        Exception ex;
        ex.m_msg.clear();
        if constexpr (sizeof...(Args) == 0) {
            ex.m_msg.assign(fmt);
        } else {
            format_into(ex.m_msg, fmt, args...);
        }
        return ex;
    }

    template <typename... Args>
    [[nodiscard]] static Exception with_error(Error err, StringRef fmt, const Args&... args) noexcept {
        Exception ex = format(fmt, args...);
        ex.m_err = err;
        return ex;
    }

    [[nodiscard]] const char* what() const noexcept override { return m_msg.c_str(); }
    [[nodiscard]] StringRef message() const noexcept { return m_msg.as_ref(); }
    [[nodiscard]] Error error() const noexcept { return m_err; }
    [[nodiscard]] ErrorCode code() const noexcept { return m_err.code; }

private:
    Error m_err{ErrorCode::Unknown, 0};
    FixedString<kBufferSize> m_msg{};
};

} // namespace keel

#endif // KEEL_HAS_EXCEPTIONS
