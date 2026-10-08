#include "network/transport/Transport.hpp"

#include <charconv>
#include <chrono>
#include <format>
#include <thread>

namespace sbx::net {

std::string Endpoint::describe() const {
    return std::format("{}:{}", host, port);
}

Expected<Endpoint> parseEndpoint(std::string_view text, u16 defaultPort) {
    if (text.empty()) {
        return makeError(ErrorCode::InvalidArgument, "주소가 비었습니다");
    }
    Endpoint ep;
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        ep.host = std::string(text);
        ep.port = defaultPort;
        return ep;
    }
    ep.host = std::string(text.substr(0, colon));
    const std::string_view portText = text.substr(colon + 1);
    unsigned value = 0;
    const auto [ptr, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), value);
    if (ec != std::errc{} || ptr != portText.data() + portText.size() || value > 65535 || ep.host.empty()) {
        return makeError(ErrorCode::InvalidArgument, "주소는 host:port 형식입니다", std::string(text));
    }
    ep.port = static_cast<u16>(value);
    return ep;
}

std::string_view disconnectReasonName(DisconnectReason r) noexcept {
    switch (r) {
    case DisconnectReason::None:
        return "없음";
    case DisconnectReason::ClientQuit:
        return "상대가 끊음";
    case DisconnectReason::ServerShutdown:
        return "서버 종료";
    case DisconnectReason::Timeout:
        return "응답 없음";
    case DisconnectReason::Rejected:
        return "거절";
    case DisconnectReason::ProtocolError:
        return "프로토콜 오류";
    case DisconnectReason::ServerFull:
        return "서버 가득 참";
    case DisconnectReason::Kicked:
        return "추방";
    case DisconnectReason::ConnectFailed:
        return "접속 실패";
    }
    return "알 수 없음";
}

void INetworkTransport::wait(u32 maxMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(maxMs));
}

} // namespace sbx::net
