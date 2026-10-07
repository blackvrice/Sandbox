#pragma once
// render/dx12 내부 공용. d3d12.h · dxgi 는 render/dx12 안에서만 include 한다 (06 R2, tools/check_includes.py).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h> // MSVC 의 d3d12.h 는 포함하지만 MinGW 는 따로
#include <dxgi1_6.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi::dx12 {

// 최소 COM 스마트 포인터 (WRL ComPtr 대신 — MSVC · MinGW 에서 같은 동작). 복사 = AddRef, 이동 = 소유권 이동.
template <class T>
class Com {
public:
    Com() = default;
    Com(std::nullptr_t) noexcept {}
    explicit Com(T* p) noexcept : m_p(p) {} // 참조를 넘겨받는다 (AddRef 안 함)
    Com(const Com& o) noexcept : m_p(o.m_p) {
        if (m_p != nullptr) {
            m_p->AddRef();
        }
    }
    Com(Com&& o) noexcept : m_p(o.m_p) { o.m_p = nullptr; }
    // 파생 → 기반 인터페이스 (ID3D12Resource → ID3D12DeviceChild)
    template <class U>
        requires std::is_convertible_v<U*, T*>
    Com(Com<U>&& o) noexcept : m_p(o.detach()) {}
    Com& operator=(Com o) noexcept {
        std::swap(m_p, o.m_p);
        return *this;
    }
    ~Com() { reset(); }

    void reset() noexcept {
        if (m_p != nullptr) {
            m_p->Release();
            m_p = nullptr;
        }
    }
    [[nodiscard]] T* get() const noexcept { return m_p; }
    // 참조를 넘겨주고 비운다 (Release 하지 않는다)
    [[nodiscard]] T* detach() noexcept {
        T* p = m_p;
        m_p = nullptr;
        return p;
    }
    T* operator->() const noexcept { return m_p; }
    explicit operator bool() const noexcept { return m_p != nullptr; }
    // 출력 인자용: 기존 참조를 놓고 주소를 준다
    T** put() noexcept {
        reset();
        return &m_p;
    }
    template <class U>
    HRESULT as(Com<U>& out) const noexcept {
        if (m_p == nullptr) {
            return E_POINTER;
        }
        return m_p->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.put()));
    }

private:
    T* m_p = nullptr;
};

[[nodiscard]] std::string hrText(HRESULT hr);
[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

[[nodiscard]] DXGI_FORMAT toDxgi(Format f) noexcept;
[[nodiscard]] D3D12_RESOURCE_STATES toD3D12(ResourceState s) noexcept;

inline void setName(ID3D12Object* o, std::string_view name) {
    if (o != nullptr && !name.empty()) {
        o->SetName(widen(name).c_str());
    }
}

} // namespace sbx::rhi::dx12
