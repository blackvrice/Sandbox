#include "render/dx12/Dx12Common.hpp"

#include <format>

namespace sbx::rhi::dx12 {

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    }
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    }
    return out;
}

std::string hrText(HRESULT hr) {
    std::string text = std::format("HRESULT 0x{:08X}", static_cast<unsigned>(hr));
    wchar_t* buf = nullptr;
    const DWORD n =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    if (n > 0 && buf != nullptr) {
        std::wstring_view w(buf, n);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) {
            w.remove_suffix(1);
        }
        text += " " + narrow(w);
    }
    if (buf != nullptr) {
        LocalFree(buf);
    }
    return text;
}

DXGI_FORMAT toDxgi(Format f) noexcept {
    switch (f) {
    case Format::R8Unorm:
        return DXGI_FORMAT_R8_UNORM;
    case Format::RG8Unorm:
        return DXGI_FORMAT_R8G8_UNORM;
    case Format::RGBA8Unorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8Unorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::RGBA16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case Format::D32Float:
        return DXGI_FORMAT_D32_FLOAT;
    case Format::D24UnormS8Uint:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::R32Uint:
        return DXGI_FORMAT_R32_UINT;
    case Format::RG32Float:
        return DXGI_FORMAT_R32G32_FLOAT;
    case Format::RGB32Float:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::RGBA32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::Unknown:
    case Format::Count:
        break;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_RESOURCE_STATES toD3D12(ResourceState s) noexcept {
    switch (s) {
    case ResourceState::Undefined:
        return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::RenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthWrite:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::DepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::ShaderRead:
        return static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    case ResourceState::UnorderedAccess:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::CopySrc:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDst:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

} // namespace sbx::rhi::dx12
