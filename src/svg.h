#pragma once

#include "Vec2.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <nanosvg.h>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace svg
{
    using geometry::Vec2f;

    struct CubicSeg
    {
        Vec2f p0, p1, p2, p3;
        float length{0.0f};
    };

    // Evaluate cubic bezier at t in [0,1]
    [[nodiscard]] static constexpr Vec2f evalCubic(const Vec2f& p0, const Vec2f& p1,
                                                   const Vec2f& p2, const Vec2f& p3,
                                                   const float t) noexcept
    {
        const float u = 1.0f - t;
        const float uu = u * u;
        const float tt = t * t;
        const float b0 = uu * u;
        const float b1 = 3.0f * uu * t;
        const float b2 = 3.0f * u * tt;
        const float b3 = tt * t;
        return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
    }

    [[nodiscard]] static inline Vec2f lerp(const Vec2f& a, const Vec2f& b,
                                           const float t) noexcept
    {
        return a + (b - a) * t;
    }

    // Estimate curve length by sampling (default samples tuned for accuracy/speed).
    [[nodiscard]] static inline float
    estimateCubicLength(const CubicSeg& s, std::size_t samples = 32) noexcept
    {
        if (samples < 1)
            return 0.0f;

        Vec2f prev = evalCubic(s.p0, s.p1, s.p2, s.p3, 0.0f);
        float total = 0.0f;

        const float invSamples = 1.0f / static_cast<float>(samples);
        for (std::size_t i = 1; i <= samples; ++i)
        {
            const float t = static_cast<float>(i) * invSamples;
            const Vec2f cur = evalCubic(s.p0, s.p1, s.p2, s.p3, t);
            total += (cur - prev).length();
            prev = cur;
        }
        return total;
    }

    struct PathPoly
    {
        std::vector<Vec2f> pts; // polyline vertices (in order)
        std::vector<float>
        cum; // cumulative distance at each vertex (same size as pts)
        float length{0.0f};
        bool closed{false};
    };


    [[nodiscard]] inline std::vector<Vec2f>
    processNSVGimage(NSVGimage* image, std::size_t number_of_points)
    {
        std::vector<Vec2f> res{};
        if (number_of_points == 0)
            return res;
        if (!image)
        {
            res.assign(number_of_points, {0.0f, 0.0f});
            return res;
        }

        // 1) collect cubic segments and estimate per-segment lengths
        std::vector<std::vector<CubicSeg>> pathSegs;
        pathSegs.reserve(64);
        float totalLenEstimate = 0.0f;

        for (NSVGshape* shape = image->shapes; shape != nullptr;
             shape = shape->next)
        {
            for (NSVGpath* path = shape->paths; path != nullptr; path = path->next)
            {
                const int npts = path->npts;
                if (npts < 2)
                    continue;

                const float* raw_pts = path->pts;
                const std::size_t float_count = static_cast<std::size_t>(npts) * 2u;
                std::span<const float> pts(raw_pts, float_count);

                Vec2f cur{pts[0], pts[1]};
                std::size_t idx = 2;

                std::vector<CubicSeg> segs;
                segs.reserve(8);

                while (idx + 5 < pts.size())
                {
                    CubicSeg s{};
                    s.p0 = cur;
                    s.p1 = Vec2f{pts[idx + 0], pts[idx + 1]};
                    s.p2 = Vec2f{pts[idx + 2], pts[idx + 3]};
                    s.p3 = Vec2f{pts[idx + 4], pts[idx + 5]};

                    s.length = estimateCubicLength(s, 32);
                    if (s.length > 1e-8f)
                    {
                        totalLenEstimate += s.length;
                        segs.emplace_back(s);
                    }

                    cur = s.p3;
                    idx += 6;
                }

                if (!segs.empty())
                    pathSegs.emplace_back(std::move(segs));
            }
        }

        if (pathSegs.empty() || totalLenEstimate <= 0.0f)
        {
            nsvgDelete(image);
            res.assign(number_of_points, {0.0f, 0.0f});
            return res;
        }

        // 2) decide oversampling step (common ds across all segs)
        constexpr float oversample_factor = 8.0f;
        const float ds =
            std::max(totalLenEstimate /
                     (static_cast<float>(number_of_points) * oversample_factor),
                     std::numeric_limits<float>::epsilon());

        // 3) build polylines for each path (oversample each cubic proportional to its
        // length)
        std::vector<PathPoly> paths;
        paths.reserve(pathSegs.size());

        constexpr float eps2 = 1e-12f;

        for (const auto& segs : pathSegs)
        {
            PathPoly poly;
            poly.closed = false;

            for (const auto& [p0, p1, p2, p3, length] : segs)
            {
                const int k =
                    std::max(2, static_cast<int>(std::lround(std::ceil(length / ds))));
                const auto denom = static_cast<float>(k - 1);
                // j from 0..k-1 inclusive
                for (int j = 0; j < k; ++j)
                {
                    const float t = (denom > 0.0f) ? (static_cast<float>(j) / denom) : 0.0f;
                    const Vec2f p = evalCubic(p0, p1, p2, p3, t);
                    if (poly.pts.empty() || (p - poly.pts.back()).length_sq() > eps2)
                    {
                        poly.pts.push_back(p);
                    }
                }
            }

            if (poly.pts.size() >= 2)
            {
                const std::size_t N = poly.pts.size();
                poly.cum.resize(N);
                poly.cum[0] = 0.0f;
                for (std::size_t i = 1; i < N; ++i)
                {
                    poly.cum[i] =
                        poly.cum[i - 1] + (poly.pts[i] - poly.pts[i - 1]).length();
                }
                poly.length = poly.cum.back();
                if (poly.length > 0.0f)
                    paths.emplace_back(std::move(poly));
            }
        }

        nsvgDelete(image);

        if (paths.empty())
        {
            res.assign(number_of_points, {0.0f, 0.0f});
            return res;
        }

        // recompute accurate total length from polylines
        float totalLen = 0.0f;
        for (const auto& p : paths)
            totalLen += p.length;
        if (totalLen <= 0.0f)
        {
            res.assign(number_of_points, paths.front().pts.front());
            return res;
        }

        // 4) Allocate counts per path proportional to path length (balanced rounding)
        const std::size_t P = paths.size();
        std::vector<std::size_t> counts(P, 0);

        struct Part
        {
            double frac;
            std::size_t idx;
        };
        std::vector<Part> fracParts;
        fracParts.reserve(P);

        std::size_t baseSum = 0;
        for (std::size_t i = 0; i < P; ++i)
        {
            const double exact =
                static_cast<double>(number_of_points) *
                (static_cast<double>(paths[i].length) / static_cast<double>(totalLen));
            const auto base = static_cast<std::size_t>(std::floor(exact));
            counts[i] = base;
            baseSum += base;
            fracParts.push_back(Part{exact - static_cast<double>(base), i});
        }

        std::size_t leftover =
            (number_of_points > baseSum) ? (number_of_points - baseSum) : 0u;
        std::ranges::sort(
            fracParts, [](const Part& a, const Part& b) { return a.frac > b.frac; });
        for (std::size_t k = 0; k < leftover && k < fracParts.size(); ++k)
        {
            counts[fracParts[k].idx] += 1;
        }

        // Guard: make finalCount equal to requested number_of_points (fix rounding
        // pathology)
        std::size_t finalCount = 0;
        for (auto c : counts)
            finalCount += c;

        if (finalCount < number_of_points)
        {
            // give remaining to longest paths
            std::vector<std::pair<float, std::size_t>> byLen;
            byLen.reserve(P);
            for (std::size_t i = 0; i < P; ++i)
                byLen.emplace_back(paths[i].length, i);
            std::ranges::sort(byLen,
                              [](auto& a, auto& b) { return a.first > b.first; });
            std::size_t deficit = number_of_points - finalCount;
            for (std::size_t i = 0; i < byLen.size() && deficit > 0; ++i, --deficit)
                counts[byLen[i].second] += 1;
            finalCount = number_of_points;
        }
        else if (finalCount > number_of_points)
        {
            // trim from shortest paths
            std::vector<std::pair<float, std::size_t>> byLen;
            byLen.reserve(P);
            for (std::size_t i = 0; i < P; ++i)
                byLen.emplace_back(paths[i].length, i);
            std::ranges::sort(byLen,
                              [](auto& a, auto& b) { return a.first < b.first; });
            std::size_t excess = finalCount - number_of_points;
            for (std::size_t i = 0; i < byLen.size() && excess > 0; ++i)
            {
                const std::size_t idx = byLen[i].second;
                const std::size_t take = std::min(excess, counts[idx]);
                counts[idx] -= take;
                excess -= take;
            }
        }

        // 5) Sample each path by inverse arc-length using binary search on its
        // polyline.
        res.reserve(number_of_points);
        for (std::size_t i = 0; i < P; ++i)
        {
            const auto& poly = paths[i];
            const std::size_t n = counts[i];
            if (n == 0)
                continue;

            const float step = poly.length / static_cast<float>(n);
            const float offset =
                0.5f * step; // center samples to avoid endpoint clustering

            for (std::size_t j = 0; j < n; ++j)
            {
                float s = offset + static_cast<float>(j) * step;
                if (s >= poly.length)
                    s = std::nextafter(poly.length, 0.0f);

                auto it = std::ranges::upper_bound(poly.cum, s);
                auto idx1 = static_cast<std::size_t>(std::distance(poly.cum.begin(), it));
                if (idx1 == 0)
                    idx1 = 1;
                if (idx1 >= poly.cum.size())
                    idx1 = poly.cum.size() - 1;
                const std::size_t idx0 = idx1 - 1;

                const float segLen = std::max(poly.cum[idx1] - poly.cum[idx0], 1e-12f);
                const float t = (s - poly.cum[idx0]) / segLen;
                const Vec2f p = lerp(poly.pts[idx0], poly.pts[idx1], t);
                res.emplace_back(p);
            }
        }

        assert(res.size() == number_of_points ||
            res.size() ==
            std::accumulate(counts.begin(), counts.end(), std::size_t{0}));
        return res;
    }

    [[nodiscard]] inline std::vector<Vec2f>
    readSVGCurveFromString(const std::string_view svg_string_view, const std::size_t number_of_points)
    {
        std::string svg_string{svg_string_view};
        NSVGimage* image = nsvgParse(svg_string.data(), "px", 96.0f);
        return processNSVGimage(image, number_of_points);
    }

    // Read an SVG path and produce `number_of_points` points distributed along all
    // paths proportionally to path length. If loading fails, returns vector of
    // zeros of requested size.
    [[nodiscard]] inline std::vector<Vec2f>
    readSVGCurveFromFile(const std::string& filename, const std::size_t number_of_points)
    {
        // parse image (caller must ensure NANOSVG_IMPLEMENTATION compiled in one TU)
        NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);

        return processNSVGimage(image, number_of_points);
    }
} // namespace svg
