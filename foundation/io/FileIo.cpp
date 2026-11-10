#include "foundation/io/FileIo.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <system_error>
#include <thread>

namespace sbx::io {

std::error_code renameRetrying(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    for (int attempt = 0;; ++attempt) {
        std::filesystem::rename(from, to, ec);
        // permission_denied = Windows ERROR_ACCESS_DENIED (MSVC STL 이 공유 위반도 이것으로) — 잠깐 잡힌 것이면 풀린다
        if (!ec || ec != std::errc::permission_denied || attempt >= 20) {
            return ec;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}

Expected<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::IoError, "파일을 열 수 없다", displayPath(path));
    }
    std::string data;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        return makeError(ErrorCode::IoError, "파일 크기를 알 수 없다", displayPath(path));
    }
    data.resize(static_cast<usize>(size));
    in.seekg(0, std::ios::beg);
    if (!data.empty() && !in.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        return makeError(ErrorCode::IoError, "읽기 실패", displayPath(path));
    }
    return data;
}

Expected<void> writeFileAtomic(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return makeError(ErrorCode::IoError, "쓰기용으로 열 수 없다", displayPath(tmp));
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec); // 반쯤 쓴 임시 파일을 남기지 않는다 (옛 파일은 그대로)
            return makeError(ErrorCode::IoError, "쓰기 실패", displayPath(tmp));
        }
    }
    // POSIX·Windows(MSVC STL: MOVEFILE_REPLACE_EXISTING) 모두 기존 파일을 바꾼다
    std::error_code ec = renameRetrying(tmp, path);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return makeError(ErrorCode::IoError, std::format("rename 실패: {}", ec.message()), displayPath(path));
    }
    return {};
}

Expected<void> createDirectories(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("디렉터리 생성 실패: {}", ec.message()), displayPath(path));
    }
    return {};
}

Expected<void> replaceDirectory(const std::filesystem::path& staging, const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::path old = target;
    old += ".old";
    std::filesystem::remove_all(old, ec); // 지난 실패의 잔재
    const bool hadTarget = std::filesystem::exists(target, ec);
    if (hadTarget) {
        ec = renameRetrying(target, old);
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("기존 폴더를 옮길 수 없다: {}", ec.message()),
                             displayPath(target));
        }
    }
    ec = renameRetrying(staging, target);
    if (ec) {
        return makeError(
            ErrorCode::IoError,
            std::format("새 폴더를 넣을 수 없다: {} (이전 내용은 {} 에 있다)", ec.message(), displayPath(old)),
            displayPath(target));
    }
    if (hadTarget) {
        std::filesystem::remove_all(old, ec); // 실패해도 저장 자체는 성공이다
    }
    return {};
}

} // namespace sbx::io
