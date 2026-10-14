#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace robustAnalysis {
constexpr unsigned gridLimit = 128;
struct Sample {
    std::array<float, 3> rgb;
    float luma;
    float x, y;
};
struct Result {
    bool valid = false;
    std::array<float, 3> black{}, white{};
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::size_t sampleCount = 0;
};
inline float luminance(const std::array<float, 3>& rgb) {
    return rgb[0] * 0.2722287168f + rgb[1] * 0.6740817658f + rgb[2] * 0.0536895174f;
}
inline bool contains(float x, float y, const float* px, const float* py) {
    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if ((py[i] > y) != (py[j] > y) &&
            x < (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i])
            inside = !inside;
    }
    return inside;
}
inline float percentile(std::vector<float>& values, float fraction) {
    const std::size_t index = static_cast<std::size_t>(fraction * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

// At most 128x128 cells, with nine taps per cell. Work and scratch memory are
// bounded independently of the scan resolution; no full-frame blur is needed.
inline Result analyze(const float* rgba, unsigned width, unsigned height,
                      const float* base, const float* px, const float* py) {
    Result result;
    if (!rgba || !width || !height) return result;
    for (int ch = 0; ch < 3; ++ch)
        if (!std::isfinite(base[ch]) || base[ch] <= 0) return result;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(px[i]) || !std::isfinite(py[i])) return result;
    const float left = std::clamp(*std::min_element(px, px + 4), 0.0f, 1.0f);
    const float right = std::clamp(*std::max_element(px, px + 4), 0.0f, 1.0f);
    const float top = std::clamp(*std::min_element(py, py + 4), 0.0f, 1.0f);
    const float bottom = std::clamp(*std::max_element(py, py + 4), 0.0f, 1.0f);
    if (right <= left || bottom <= top) return result;
    const unsigned columns = std::min(gridLimit, std::max(1u, static_cast<unsigned>((right - left) * width)));
    const unsigned rows = std::min(gridLimit, std::max(1u, static_cast<unsigned>((bottom - top) * height)));
    const float dx = (right - left) / columns, dy = (bottom - top) / rows;
    std::vector<Sample> samples;
    samples.reserve(columns * rows);
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned col = 0; col < columns; ++col) {
            const float cx = left + (col + 0.5f) * dx, cy = top + (row + 0.5f) * dy;
            if (!contains(cx, cy, px, py)) continue;
            std::array<std::array<float, 9>, 3> taps;
            unsigned count = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const unsigned x = std::min(width - 1, static_cast<unsigned>((cx + ox * dx * 0.25f) * width));
                    const unsigned y = std::min(height - 1, static_cast<unsigned>((cy + oy * dy * 0.25f) * height));
                    if (!contains((x + 0.5f) / width, (y + 0.5f) / height, px, py)) continue;
                    const float* pixel = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                    if (!std::isfinite(pixel[0]) || !std::isfinite(pixel[1]) || !std::isfinite(pixel[2])) continue;
                    for (int ch = 0; ch < 3; ++ch) taps[ch][count] = pixel[ch];
                    ++count;
                }
            }
            if (!count) continue;
            Sample sample{{}, 0, cx, cy};
            for (int ch = 0; ch < 3; ++ch) {
                auto first = taps[ch].begin();
                std::nth_element(first, first + count / 2, first + count);
                sample.rgb[ch] = base[ch] / std::max(taps[ch][count / 2], 0.0001f) * 0.1f;
            }
            sample.luma = luminance(sample.rgb);
            if (std::isfinite(sample.luma)) samples.push_back(sample);
        }
    }
    result.sampleCount = samples.size();
    if (samples.size() < 16) return result;
    std::vector<float> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) values.push_back(sample.luma);
    const float low = percentile(values, 0.005f), high = percentile(values, 0.995f);
    if (!std::isfinite(high - low) || high - low <= std::max(1e-6f, high * 1e-5f)) return result;
    const float shadowBand = percentile(values, 0.2f), highlightBand = percentile(values, 0.8f);
    auto endpoint = [&](bool highlight) {
        std::array<std::vector<float>, 3> neutrals;
        for (const auto& sample : samples) {
            const bool inBand = highlight ? sample.luma >= highlightBand : sample.luma <= shadowBand;
            const auto [min, max] = std::minmax_element(sample.rgb.begin(), sample.rgb.end());
            if (inBand && *max - *min <= 0.15f * sample.luma) {
                for (int ch = 0; ch < 3; ++ch) neutrals[ch].push_back(sample.rgb[ch]);
            }
        }
        std::array<float, 3> rgb;
        for (int ch = 0; ch < 3; ++ch) {
            if (neutrals[ch].size() >= 8) {
                rgb[ch] = percentile(neutrals[ch], 0.5f);
            } else {
                values.clear();
                for (const auto& sample : samples) values.push_back(sample.rgb[ch]);
                rgb[ch] = percentile(values, highlight ? 0.995f : 0.005f);
            }
        }
        // Color deviations are anchored to the independently metered tone.
        const float center = luminance(rgb), tone = highlight ? high : low;
        for (float& channel : rgb) channel += tone - center;
        return rgb;
    };
    result.black = endpoint(false);
    result.white = endpoint(true);
    for (int ch = 0; ch < 3; ++ch) {
        if (!std::isfinite(result.black[ch]) || !std::isfinite(result.white[ch]) ||
            result.white[ch] - result.black[ch] <= (high - low) * 1e-4f) {
            // Insufficient channel range: retain the metered tone without an
            // unreliable channel-specific correction.
            result.black.fill(low);
            result.white.fill(high);
            break;
        }
    }
    const auto min = std::min_element(samples.begin(), samples.end(), [&](const Sample& a, const Sample& b) {
        return std::abs(a.luma - low) < std::abs(b.luma - low);
    });
    const auto max = std::min_element(samples.begin(), samples.end(), [&](const Sample& a, const Sample& b) {
        return std::abs(a.luma - high) < std::abs(b.luma - high);
    });
    result.minX = min->x; result.minY = min->y;
    result.maxX = max->x; result.maxY = max->y;
    result.valid = true;
    return result;
}
} // namespace robustAnalysis
