#pragma once
// N 개까지는 객체 안에 저장하고, 넘치면 힙으로 옮기는 벡터.
// 컴포넌트 안의 짧은 가변 목록(센서 감지 목록, 경로 웨이포인트)과 핫 루프의 임시 버퍼에 쓴다.
//
// 규칙
// - 힙으로 한 번 넘어가면 clear() 해도 인라인으로 돌아가지 않는다 (용량 유지). shrinkToFit() 으로만 돌아간다.
// - 원소 순서·연산 의미는 std::vector 와 같다. 결정론에 영향을 주는 동작은 없다.
// - 반복자는 원시 포인터다. 재할당이 일어나는 연산 후에는 무효가 된다 (std::vector 와 동일).

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "foundation/assert/Assert.hpp"
#include "foundation/types/Types.hpp"

namespace sbx {

template <class T, usize N>
class SmallVector {
    static_assert(N > 0, "인라인 용량은 1 이상이어야 한다");

public:
    using value_type = T;
    using size_type = usize;
    using iterator = T*;
    using const_iterator = const T*;

    SmallVector() noexcept = default;

    SmallVector(std::initializer_list<T> init) {
        reserve(init.size());
        for (const T& v : init) {
            emplace_back(v);
        }
    }

    SmallVector(const SmallVector& other) {
        reserve(other.m_size);
        for (const T& v : other) {
            emplace_back(v);
        }
    }

    SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) { moveFrom(std::move(other)); }

    SmallVector& operator=(const SmallVector& other) {
        if (this != &other) {
            clear();
            reserve(other.m_size);
            for (const T& v : other) {
                emplace_back(v);
            }
        }
        return *this;
    }

    SmallVector& operator=(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            destroyAll();
            releaseHeap();
            moveFrom(std::move(other));
        }
        return *this;
    }

    ~SmallVector() {
        destroyAll();
        releaseHeap();
    }

    // --- 접근 -------------------------------------------------------------
    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }
    [[nodiscard]] usize size() const noexcept { return m_size; }
    [[nodiscard]] usize capacity() const noexcept { return m_capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool isInline() const noexcept { return m_data == inlineData(); }
    [[nodiscard]] static constexpr usize inlineCapacity() noexcept { return N; }

    T& operator[](usize i) noexcept {
        SBX_ASSERT(i < m_size, "SmallVector index out of range");
        return m_data[i];
    }
    const T& operator[](usize i) const noexcept {
        SBX_ASSERT(i < m_size, "SmallVector index out of range");
        return m_data[i];
    }
    T& front() noexcept { return (*this)[0]; }
    T& back() noexcept { return (*this)[m_size - 1]; }
    const T& front() const noexcept { return (*this)[0]; }
    const T& back() const noexcept { return (*this)[m_size - 1]; }

    iterator begin() noexcept { return m_data; }
    iterator end() noexcept { return m_data + m_size; }
    const_iterator begin() const noexcept { return m_data; }
    const_iterator end() const noexcept { return m_data + m_size; }

    // --- 변경 -------------------------------------------------------------
    template <class... Args>
    T& emplace_back(Args&&... args) {
        if (m_size == m_capacity) {
            // 인자가 자기 원소를 참조할 수 있으므로(v.push_back(v[0])) 먼저 새 위치에 만들고 나서 옮긴다.
            const usize newCap = growCapacity(m_size + 1);
            T* newData = allocate(newCap);
            ::new (static_cast<void*>(newData + m_size)) T(std::forward<Args>(args)...);
            relocate(newData);
            m_data = newData;
            m_capacity = newCap;
        } else {
            ::new (static_cast<void*>(m_data + m_size)) T(std::forward<Args>(args)...);
        }
        return m_data[m_size++];
    }

    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }

    void pop_back() noexcept {
        SBX_ASSERT(m_size > 0, "pop_back on empty SmallVector");
        --m_size;
        std::destroy_at(m_data + m_size);
    }

    void clear() noexcept { destroyAll(); }

    void reserve(usize n) {
        if (n <= m_capacity) {
            return;
        }
        T* newData = allocate(n);
        relocate(newData);
        m_data = newData;
        m_capacity = n;
    }

    void resize(usize n) {
        if (n < m_size) {
            std::destroy(m_data + n, m_data + m_size);
            m_size = n;
            return;
        }
        reserve(n);
        while (m_size < n) {
            ::new (static_cast<void*>(m_data + m_size)) T();
            ++m_size;
        }
    }

    // 순서를 유지하며 제거. 뒤 원소를 한 칸씩 당긴다. O(n)
    iterator erase(const_iterator pos) {
        const usize i = static_cast<usize>(pos - m_data);
        SBX_ASSERT(i < m_size, "erase position out of range");
        std::move(m_data + i + 1, m_data + m_size, m_data + i);
        pop_back();
        return m_data + i;
    }

    // 순서를 버리고 제거. 마지막 원소를 빈자리로 옮긴다. O(1)
    void eraseUnordered(usize i) {
        SBX_ASSERT(i < m_size, "eraseUnordered index out of range");
        if (i != m_size - 1) {
            m_data[i] = std::move(m_data[m_size - 1]);
        }
        pop_back();
    }

    // 힙을 쓰고 있고 원소가 인라인 용량에 들어가면 인라인으로 돌아간다.
    void shrinkToFit() {
        if (isInline() || m_size > N) {
            return;
        }
        T* heap = m_data;
        T* dst = inlineData();
        std::uninitialized_move(heap, heap + m_size, dst);
        std::destroy(heap, heap + m_size);
        ::operator delete(static_cast<void*>(heap), std::align_val_t{alignof(T)});
        m_data = dst;
        m_capacity = N;
    }

    friend bool operator==(const SmallVector& a, const SmallVector& b) {
        return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin());
    }

private:
    T* inlineData() noexcept { return std::launder(reinterpret_cast<T*>(m_inline)); }
    const T* inlineData() const noexcept { return std::launder(reinterpret_cast<const T*>(m_inline)); }

    static T* allocate(usize n) { return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{alignof(T)})); }

    static usize growCapacity(usize minimum) noexcept { return std::max(minimum, minimum * 3 / 2 + 1); }

    // 현재 원소를 newData 로 옮기고 이전 저장소를 정리한다 (크기는 그대로).
    void relocate(T* newData) {
        // 명시적 루프: GCC 13 -O2 가 std::uninitialized_move 의 memmove 를 인라인 버퍼에 대해
        // 범위 밖 접근으로 오진(-Warray-bounds)한다. 동작은 같다.
        for (usize i = 0; i < m_size; ++i) {
            ::new (static_cast<void*>(newData + i)) T(std::move(m_data[i]));
            std::destroy_at(m_data + i);
        }
        releaseHeap();
    }

    void destroyAll() noexcept {
        std::destroy(m_data, m_data + m_size);
        m_size = 0;
    }

    void releaseHeap() noexcept {
        if (!isInline()) {
            ::operator delete(static_cast<void*>(m_data), std::align_val_t{alignof(T)});
        }
        m_data = inlineData();
        m_capacity = N;
    }

    // 이 객체는 비어 있고 인라인 상태여야 한다.
    void moveFrom(SmallVector&& other) {
        if (other.isInline()) {
            std::uninitialized_move(other.m_data, other.m_data + other.m_size, m_data);
            m_size = other.m_size;
            other.destroyAll();
        } else {
            // 힙 버퍼는 소유권만 넘긴다
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = other.inlineData();
            other.m_size = 0;
            other.m_capacity = N;
        }
    }

    alignas(T) std::byte m_inline[N * sizeof(T)];
    T* m_data = inlineData();
    usize m_size = 0;
    usize m_capacity = N;
};

} // namespace sbx
