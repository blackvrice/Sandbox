#pragma once
// 2D 벡터. 월드 좌표는 float (docs/05-WORLD.md 1장), 타일·청크 좌표는 int32 (Vec2i).

#include <cmath>
#include <compare>

#include "foundation/types/Types.hpp"

namespace sbx {

struct Vec2 {
    f32 x = 0.f;
    f32 y = 0.f;

    friend constexpr bool operator==(const Vec2&, const Vec2&) noexcept = default;

    constexpr Vec2 operator+(Vec2 o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(f32 s) const noexcept { return {x * s, y * s}; }
    constexpr Vec2 operator/(f32 s) const noexcept { return {x / s, y / s}; }
    constexpr Vec2 operator-() const noexcept { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) noexcept {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(Vec2 o) noexcept {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    constexpr Vec2& operator*=(f32 s) noexcept {
        x *= s;
        y *= s;
        return *this;
    }

    [[nodiscard]] constexpr f32 dot(Vec2 o) const noexcept { return x * o.x + y * o.y; }
    [[nodiscard]] constexpr f32 lengthSquared() const noexcept { return dot(*this); }
    [[nodiscard]] f32 length() const noexcept { return std::sqrt(lengthSquared()); }
};

constexpr Vec2 operator*(f32 s, Vec2 v) noexcept {
    return v * s;
}

struct Vec2i {
    i32 x = 0;
    i32 y = 0;

    friend constexpr bool operator==(const Vec2i&, const Vec2i&) noexcept = default;
    friend constexpr auto operator<=>(const Vec2i&, const Vec2i&) noexcept = default; // y 가 아니라 x 우선

    constexpr Vec2i operator+(Vec2i o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(Vec2i o) const noexcept { return {x - o.x, y - o.y}; }
};

} // namespace sbx
