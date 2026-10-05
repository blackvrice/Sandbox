#pragma once
// 복구 가능한 실패의 표현. docs/12-CODING-STANDARDS.md 5장.
//
// - 프로그래머 오류(불변식 위반)는 SBX_ASSERT 로.
// - 외부 입력(파일, 네트워크, 콘텐츠)의 실패는 반드시 Expected<T> 로 돌려준다.
// - 엔진 코드는 예외를 던지지 않는다. 서드파티 예외는 경계에서 잡아 Error 로 바꾼다.

#include <expected>
#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx {

enum class ErrorCode : u16 {
    Unknown = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    ParseError,
    ValidationFailed,
    VersionMismatch,
    IoError,
    Unsupported,
    OutOfRange,
    PermissionDenied,
};

[[nodiscard]] std::string_view errorCodeName(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message; // 사람이 읽는 원인
    std::string context; // 위치: "파일:JSON 포인터", "tick 1200, netId 42" 등. 없으면 빈 문자열

    // "ParseError: <message> [<context>]"
    [[nodiscard]] std::string describe() const;
};

template <class T>
using Expected = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message, std::string context = {}) {
    return std::unexpected<Error>(Error{code, std::move(message), std::move(context)});
}

} // namespace sbx
