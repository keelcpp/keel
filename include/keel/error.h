#pragma once

#include "keel/ref.h"

namespace keel {

enum class ErrorCode : u16 {
    Ok = 0,
    NotFound,
    PermissionDenied,
    AlreadyExists,
    InvalidArgument,
    InvalidData,
    OutOfRange,
    Overflow,
    IoError,
    EndOfFile,
    WouldBlock,
    NotSupported,
    Unknown
};

struct [[nodiscard]] Error {
    ErrorCode code = ErrorCode::Ok;
    i32 os_error = 0;

    constexpr Error() noexcept = default;
    constexpr Error(ErrorCode c, i32 os = 0) noexcept : code(c), os_error(os) {}

    [[nodiscard]] static constexpr Error ok() noexcept { return Error{ErrorCode::Ok, 0}; }

    [[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return code != ErrorCode::Ok; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return code == ErrorCode::Ok; }

    [[nodiscard]] constexpr const Error& error() const noexcept { return *this; }

    [[nodiscard]] constexpr bool operator==(const Error& other) const noexcept = default;
};

struct [[nodiscard]] ParseError {
    ErrorCode code = ErrorCode::Ok;
    usize offset = 0;

    constexpr ParseError() noexcept = default;
    constexpr ParseError(ErrorCode c, usize off = 0) noexcept : code(c), offset(off) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return code != ErrorCode::Ok; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return code == ErrorCode::Ok; }

    [[nodiscard]] constexpr bool operator==(const ParseError& other) const noexcept = default;
};

[[nodiscard]] StringRef error_message(ErrorCode code) noexcept;
[[nodiscard]] inline StringRef error_message(Error err) noexcept { return error_message(err.code); }

} // namespace keel
