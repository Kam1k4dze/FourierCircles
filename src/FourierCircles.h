#pragma once
#include <algorithm>
#include <cassert>
#include <ranges>
#include <vector>
#include <numbers>
#include <random>
#include "Vec2.h"
#include "fft.h"

struct FourierCircles
{
    using Vec2f = geometry::Vec2f;
    using Vector = std::vector<Vec2f>;

    void calculateCoefficients(const Vector& input)
    {
        coefficients = fft(input);
        sortedIndices.resize(coefficients.size());
        std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
        std::ranges::sort(sortedIndices, [&](const size_t i, const size_t j)
        {
            return coefficients[i].length_sq() > coefficients[j].length_sq();
        });
    }

    [[nodiscard]] Vec2f getResult() const { return result; }
    [[nodiscard]] const Vector& getVectors() const { return vectors; }

    void calculateVectors(const float t)
    {
        assert(coefficients.size() == sortedIndices.size());
        constexpr float PI = std::numbers::pi_v<float>;
        const size_t size = coefficients.size();
        vectors.resize(size);
        result = {0, 0};

        if (size == 0) return;

        const float two_pi_t = 2.0f * PI * t;
        size_t index = 0;
        for (const size_t n : sortedIndices)
        {
            // compute signed frequency k as int for negative values when n>size/2
            const int k = (n <= (size >> 1)) ? static_cast<int>(n) : static_cast<int>(n) - static_cast<int>(size);
            const float phase = two_pi_t * static_cast<float>(k);

            const float a = coefficients[n].x;
            const float b = coefficients[n].y;

            float s, c;
            sincosf(phase, &s, &c);
            const Vec2f vec{a * c - b * s, a * s + b * c};
            result += vec;
            vectors[index++] = vec;
        }
    }

private:
    fft::FFT fft_plan{};

    [[nodiscard]] Vector fft(const Vector& input)
    {
        if (input.empty())
            return {};

        const auto N = input.size();

        if (fft_plan.size() == 0 || fft_plan.size() != N)
        {
            fft_plan = fft::FFT(N, fft::FFTDirection::Forward);
        }
        Vector output(N);
        fft_plan.execute(input, output);

        // Normalize by N
        const float invN = 1.0f / static_cast<float>(N);
        for (int i = 0; i < N; ++i)
        {
            output[i] *= invN;
        }
        return output;
    }

    std::vector<Vec2f> coefficients{};
    std::vector<size_t> sortedIndices{};
    Vector vectors{};
    Vec2f result{};
};
