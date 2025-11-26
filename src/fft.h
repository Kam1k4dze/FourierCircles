#pragma once

#include <vector>
#include <span>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <stdexcept>
#include <algorithm>
#include <bit>
#include "Vec2.h"

namespace fft
{
    using geometry::Vec2f;

    enum class FFTDirection : int8_t
    {
        Forward = +1,
        Inverse = -1
    };

    // Internal helpers
    namespace detail
    {
        inline Vec2f cadd(const Vec2f& a, const Vec2f& b) noexcept { return {a.x + b.x, a.y + b.y}; }
        inline Vec2f csub(const Vec2f& a, const Vec2f& b) noexcept { return {a.x - b.x, a.y - b.y}; }

        inline Vec2f cmul(const Vec2f& a, const Vec2f& b) noexcept
        {
            return {
                a.x * b.x - a.y * b.y,
                a.x * b.y + a.y * b.x
            };
        }

        inline Vec2f cconj(const Vec2f& a) noexcept { return {a.x, -a.y}; }
        inline Vec2f cscale(const Vec2f& a, const float s) noexcept { return {a.x * s, a.y * s}; }

        inline bool is_pow2(const std::size_t n) noexcept
        {
            return n != 0 && (n & (n - 1)) == 0;
        }

        inline constexpr float PI = std::numbers::pi_v<float>;
    } // namespace detail

    struct FFT
    {
        FFT() = default;

        explicit FFT(const std::size_t n, const FFTDirection dir = FFTDirection::Forward)
            : n_{n}, dir_{dir}
        {
            if (n_ == 0)
                throw std::invalid_argument("FFT size must be > 0");

            pow2_ = detail::is_pow2(n_);

            if (pow2_)
            {
                scratch_.resize(n_);
            }
            else
            {
                m_ = std::bit_ceil(2 * n_ - 1);
                A_.assign(m_, {});
                B_.assign(m_, {});
                conv_.assign(m_, {});
                precompute_bluestein();
            }
        }

        void execute(const std::span<const Vec2f> in, const std::span<Vec2f> out) const
        {
            if (in.size() != n_ || out.size() != n_)
                throw std::invalid_argument("FFT::execute: size mismatch");

            if (pow2_)
                fft_radix2(in, out);
            else
                fft_bluestein(in, out);
        }

        std::vector<Vec2f> operator()(const std::vector<Vec2f>& in) const
        {
            std::vector<Vec2f> out(in.size());
            execute(in, out);
            return out;
        }

        std::size_t size() const noexcept { return n_; }
        FFTDirection direction() const noexcept { return dir_; }
        bool is_power_of_two() const noexcept { return pow2_; }

    private:
        std::size_t n_{};
        FFTDirection dir_{FFTDirection::Forward};
        bool pow2_{false};

        // Radix-2 buffer
        mutable std::vector<Vec2f> scratch_;

        // Bluestein buffers
        std::size_t m_{};
        std::vector<Vec2f> chirp_; // size n
        mutable std::vector<Vec2f> A_; // size m
        mutable std::vector<Vec2f> B_; // size m
        mutable std::vector<Vec2f> conv_;


        static void fft_radix2_inplace(std::vector<Vec2f>& a, const FFTDirection dir)
        {
            using namespace detail;

            const std::size_t n = a.size();
            if (n <= 1) return;

            // bit reverse permutation
            for (std::size_t i = 1, j = 0; i < n; ++i)
            {
                std::size_t bit = n >> 1;
                while (j & bit)
                {
                    j ^= bit;
                    bit >>= 1;
                }
                j |= bit;
                if (i < j) std::swap(a[i], a[j]);
            }

            const float sign = (dir == FFTDirection::Forward) ? -1.0f : +1.0f;

            for (std::size_t len = 2; len <= n; len <<= 1)
            {
                const float ang = sign * 2.0f * PI / static_cast<float>(len);
                const float c0 = std::cos(ang);
                const float s0 = std::sin(ang);

                for (std::size_t i = 0; i < n; i += len)
                {
                    Vec2f w{1.0f, 0.0f};
                    const std::size_t half = len >> 1;

                    for (std::size_t j = 0; j < half; ++j)
                    {
                        Vec2f u = a[i + j];
                        Vec2f v = cmul(a[i + j + half], w);

                        a[i + j] = cadd(u, v);
                        a[i + j + half] = csub(u, v);

                        float wx = w.x * c0 - w.y * s0;
                        float wy = w.x * s0 + w.y * c0;
                        w = {wx, wy};
                    }
                }
            }

            if (dir == FFTDirection::Inverse)
            {
                const float inv = 1.0f / static_cast<float>(n);
                for (auto& z : a) z = cscale(z, inv);
            }
        }

        void fft_radix2(std::span<const Vec2f> in, std::span<Vec2f> out) const
        {
            std::ranges::copy(in, scratch_.begin());
            fft_radix2_inplace(scratch_, dir_);
            std::copy_n(scratch_.begin(), n_, out.begin());
        }

        void precompute_bluestein()
        {
            using namespace detail;

            chirp_.resize(n_);

            const float s = (dir_ == FFTDirection::Forward) ? +1.0f : -1.0f;

            for (std::size_t k = 0; k < n_; ++k)
            {
                const float kk = static_cast<float>(k) * static_cast<float>(k);
                const float ang = s * PI * kk / static_cast<float>(n_);
                chirp_[k] = {std::cos(ang), std::sin(ang)};
            }

            std::ranges::fill(B_, Vec2f{});
            for (std::size_t k = 0; k < n_; ++k)
            {
                const Vec2f c = cconj(chirp_[k]);
                B_[k] = c;
                if (k != 0) B_[m_ - k] = c;
            }
        }

        void fft_bluestein(const std::span<const Vec2f> in, std::span<Vec2f> out) const
        {
            using namespace detail;

            // A[k] = in[k] * chirp[k]
            std::ranges::fill(A_, Vec2f{});
            for (std::size_t k = 0; k < n_; ++k)
                A_[k] = cmul(in[k], chirp_[k]);

            // Copy B to conv_
            conv_ = B_;

            // FFT(A) and FFT(conv_)
            fft_radix2_inplace(A_, FFTDirection::Forward);
            fft_radix2_inplace(conv_, FFTDirection::Forward);

            // pointwise multiply
            for (std::size_t i = 0; i < m_; ++i)
                A_[i] = cmul(A_[i], conv_[i]);

            // inverse FFT
            fft_radix2_inplace(A_, FFTDirection::Inverse);

            // final multiply by chirp
            for (std::size_t k = 0; k < n_; ++k)
                out[k] = cmul(A_[k], chirp_[k]);
        }
    };
} // namespace fft
