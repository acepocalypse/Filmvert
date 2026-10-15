#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace autoCrop {
struct Bounds {
    float left, top, right, bottom;
};

// Inspect the unmodified scan, before film inversion or grading. Sample rays
// from each edge; sustained light pixels separate the frame from black padding.
// Percentiles reject dust and allow a slightly tilted or rounded film frame.
inline std::optional<Bounds> detect(const float* rgba, unsigned width, unsigned height,
                                    float thresholdFraction = 0.08f, float inset = 0.005f) {
    if (!rgba || width < 32 || height < 32 || !std::isfinite(thresholdFraction) ||
        !std::isfinite(inset)) return std::nullopt;
    thresholdFraction = std::clamp(thresholdFraction, 0.01f, 0.3f);
    inset = std::clamp(inset, 0.0f, 0.03f);
    auto brightness = [&](unsigned x, unsigned y) {
        const float* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return 0.0f;
        return std::max({0.0f, p[0], p[1], p[2]});
    };
    auto percentile = [](std::vector<float>& values, float fraction) {
        const auto index = static_cast<std::size_t>(fraction * (values.size() - 1));
        std::nth_element(values.begin(), values.begin() + index, values.end());
        return values[index];
    };
    std::vector<float> reference;
    for (unsigned y = 0; y < 48; ++y)
        for (unsigned x = 0; x < 48; ++x)
            reference.push_back(brightness(static_cast<unsigned>((0.2f + 0.6f * (x + 0.5f) / 48) * width),
                                           static_cast<unsigned>((0.2f + 0.6f * (y + 0.5f) / 48) * height)));
    const float level = percentile(reference, 0.75f);
    if (level <= 1e-8f) return std::nullopt;
    const float threshold = level * thresholdFraction;
    float trims[4] = {};
    for (int edge = 0; edge < 4; ++edge) {
        const bool horizontal = edge < 2;
        const bool reverse = edge % 2 != 0;
        const unsigned length = horizontal ? width : height;
        const unsigned across = horizontal ? height : width;
        const unsigned step = std::max(1u, length / 1024);
        std::vector<float> distances;
        for (unsigned ray = 0; ray < 96; ++ray) {
            const unsigned fixed = std::min(across - 1,
                static_cast<unsigned>((0.025f + 0.95f * (ray + 0.5f) / 96) * across));
            for (unsigned d = 0; d + 2 * step < length / 5; d += step) {
                bool content = true;
                for (unsigned tap = 0; tap < 3; ++tap) {
                    const unsigned pos = reverse ? length - 1 - d - tap * step : d + tap * step;
                    if (brightness(horizontal ? pos : fixed, horizontal ? fixed : pos) <= threshold) {
                        content = false;
                        break;
                    }
                }
                if (content) {
                    distances.push_back(static_cast<float>(d) / length);
                    break;
                }
            }
        }
        // Avoid guessing when the image is dark or the border is too wide.
        if (distances.size() < 80) return std::nullopt;
        const float trim = percentile(distances, 0.98f);
        // An edge with no black padding should not acquire an arbitrary inset.
        trims[edge] = trim > 0.0f ? trim + inset + static_cast<float>(step) / length : 0.0f;
    }
    if (trims[0] + trims[1] + trims[2] + trims[3] == 0.0f) return std::nullopt;
    return Bounds{trims[0], trims[2], 1.0f - trims[1], 1.0f - trims[3]};
}
} // namespace autoCrop
