#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ostream>
#include <type_traits>

namespace geometry
{
    template <std::floating_point T>
    struct Vec2
    {
        T x{};
        T y{};

        // --- ctors ---
        constexpr Vec2() noexcept = default;

        constexpr explicit Vec2(T v) noexcept : x(v), y(v)
        {
        }

        constexpr Vec2(T x_, T y_) noexcept : x(x_), y(y_)
        {
        }

        // allow implicit construction from other floating point types but avoid
        // narrowing
        template <std::floating_point U>
        explicit(false) constexpr Vec2(const Vec2<U>& o) noexcept
            : x(static_cast<T>(o.x)), y(static_cast<T>(o.y))
        {
        }

        // --- element access ---
        constexpr T operator[](const std::size_t i) const noexcept
        {
            return i == 0 ? x : y;
        }

        constexpr T& operator[](const std::size_t i) noexcept { return (i == 0) ? x : y; }

        // --- comparisons ---
        constexpr auto operator<=>(const Vec2&) const noexcept = default;

        // --- unary ---
        constexpr Vec2 operator+() const noexcept { return *this; }
        constexpr Vec2 operator-() const noexcept { return Vec2(-x, -y); }

        // --- arithmetic (vec op vec) ---
        constexpr Vec2& operator+=(const Vec2& o) noexcept
        {
            x += o.x;
            y += o.y;
            return *this;
        }

        constexpr Vec2& operator-=(const Vec2& o) noexcept
        {
            x -= o.x;
            y -= o.y;
            return *this;
        }

        constexpr Vec2& operator*=(T s) noexcept
        {
            x *= s;
            y *= s;
            return *this;
        }

        constexpr Vec2& operator/=(T s) noexcept
        {
            x /= s;
            y /= s;
            return *this;
        }

        // --- utilities ---
        [[nodiscard]] constexpr T dot(const Vec2& o) const noexcept { return x * o.x + y * o.y; }

        [[nodiscard]] constexpr T cross(const Vec2& o) const noexcept
        {
            return x * o.y - y * o.x;
        } // scalar 2D cross
        [[nodiscard]] constexpr T length_sq() const noexcept { return x * x + y * y; }
        [[nodiscard]] T length() const noexcept { return std::hypot(x, y); }

        [[nodiscard]] Vec2 normalized() const noexcept
        {
            const T l = length();
            return (l == T(0)) ? Vec2(T(0), T(0)) : (*this) / l;
        }

        Vec2& normalize() noexcept
        {
            const T l = length();
            if (l != T(0))
            {
                x /= l;
                y /= l;
            }
            return *this;
        }

        // --- swizzle helpers (common) ---
        [[nodiscard]] constexpr Vec2 yx() const noexcept { return Vec2(y, x); }

        // --- named constants ---
        [[nodiscard]] static constexpr Vec2 zero() noexcept { return Vec2(T(0), T(0)); }
        [[nodiscard]] static constexpr Vec2 one() noexcept { return Vec2(T(1), T(1)); }
        [[nodiscard]] static constexpr Vec2 unit_x() noexcept { return Vec2(T(1), T(0)); }
        [[nodiscard]] static constexpr Vec2 unit_y() noexcept { return Vec2(T(0), T(1)); }
    };

    // free operators
    template <std::floating_point T>
    [[nodiscard]] constexpr Vec2<T> operator+(Vec2<T> a, const Vec2<T>& b) noexcept
    {
        a += b;
        return a;
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Vec2<T> operator-(Vec2<T> a, const Vec2<T>& b) noexcept
    {
        a -= b;
        return a;
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Vec2<T> operator*(Vec2<T> v, T s) noexcept
    {
        v *= s;
        return v;
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Vec2<T> operator*(T s, Vec2<T> v) noexcept
    {
        v *= s;
        return v;
    }

    template <std::floating_point T>
    [[nodiscard]] constexpr Vec2<T> operator/(Vec2<T> v, T s) noexcept
    {
        v /= s;
        return v;
    }

    template <std::floating_point T>
    constexpr Vec2<T> operator/(T s, Vec2<T> v) = delete; // nonsensical

    // stream output
    template <std::floating_point T>
    inline std::ostream& operator<<(std::ostream& os, const Vec2<T>& v)
    {
        return os << '(' << v.x << ", " << v.y << ')';
    }

    using Vec2f = Vec2<float>;
    using Vec2d = Vec2<double>;
} // namespace geometry

// std::hash specialization
template <std::floating_point T>
struct std::hash<geometry::Vec2<T>>
{
    std::size_t operator()(geometry::Vec2<T> const& v) const noexcept
    {
        const std::size_t h1 = std::hash<T>{}(v.x);
        const std::size_t h2 = std::hash<T>{}(v.y);
        // combine (from boost)
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
