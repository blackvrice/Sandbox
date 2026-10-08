#include "foundation/types/Error.hpp"

namespace sbx {

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::ParseError:
        return "ParseError";
    case ErrorCode::ValidationFailed:
        return "ValidationFailed";
    case ErrorCode::VersionMismatch:
        return "VersionMismatch";
    case ErrorCode::IoError:
        return "IoError";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::OutOfRange:
        return "OutOfRange";
    case ErrorCode::PermissionDenied:
        return "PermissionDenied";
    case ErrorCode::RateLimited:
        return "RateLimited";
    }
    return "Unknown";
}

std::string Error::describe() const {
    std::string out{errorCodeName(code)};
    out += ": ";
    out += message;
    if (!context.empty()) {
        out += " [";
        out += context;
        out += ']';
    }
    return out;
}

} // namespace sbx
