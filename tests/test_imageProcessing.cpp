#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>
#include <chrono>
#include <iostream>
#include <limits>
#include "robustAnalysis.h"
#include "window/imageViewCoordinates.h"
#include "preferences.h"
#include "image.h"         // float4
#include "renderParams.h"  // renderParams, CURVE_MAX_PTS

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
std::vector<float> negativeRamp(unsigned w, unsigned h) {
    std::vector<float> pixels(static_cast<std::size_t>(w) * h * 4, 1.0f);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
            for (int ch = 0; ch < 3; ++ch)
                pixels[(static_cast<std::size_t>(y) * w + x) * 4 + ch] = 0.1f / (0.1f + 0.9f * x / (w - 1));
    return pixels;
}
robustAnalysis::Result meter(const std::vector<float>& pixels, unsigned w, unsigned h,
                             const imageParams& params) {
    const float base[] = {1, 1, 1};
    return robustAnalysis::analyze(pixels.data(), w, h, base, params.cropBoxX, params.cropBoxY);
}
imageParams fullAnalysisRegion() {
    imageParams params;
    const float x[] = {0, 1, 1, 0}, y[] = {0, 0, 1, 1};
    std::copy(x, x + 4, params.cropBoxX);
    std::copy(y, y + 4, params.cropBoxY);
    return params;
}
}

TEST_CASE("Robust analysis ignores sparse dust and preserves grayscale neutrality", "[robust]") {
    auto pixels = negativeRamp(256, 256);
    const auto params = fullAnalysisRegion();
    const auto clean = meter(pixels, 256, 256, params);
    REQUIRE(clean.valid);
    for (std::size_t i = 37; i < pixels.size() / 4; i += 997)
        for (int ch = 0; ch < 3; ++ch) pixels[i * 4 + ch] = 0;
    const auto dusty = meter(pixels, 256, 256, params);
    REQUIRE(dusty.valid);
    for (int ch = 0; ch < 3; ++ch) {
        CHECK_THAT(dusty.black[ch], WithinAbs(clean.black[ch], 0.01f));
        CHECK_THAT(dusty.white[ch], WithinAbs(clean.white[ch], 0.01f));
        CHECK_THAT(dusty.white[ch], WithinAbs(dusty.white[0], 1e-6f));
    }
}

TEST_CASE("Robust neutral color estimates do not inherit a saturated highlight", "[robust]") {
    auto pixels = negativeRamp(256, 128);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 248; x < 256; ++x) {
            auto* pixel = pixels.data() + (y * 256 + x) * 4;
            pixel[0] = 0.1f / 4.0f;
            pixel[1] = pixel[2] = 1.0f;
        }
    const auto result = meter(pixels, 256, 128, fullAnalysisRegion());
    REQUIRE(result.valid);
    CHECK_THAT(result.white[0], WithinAbs(result.white[1], 1e-6f));
    CHECK_THAT(result.white[1], WithinAbs(result.white[2], 1e-6f));
    CHECK(result.white[0] > 1.0f); // Retains the independently metered highlight brightness.
}

TEST_CASE("Robust metering follows the applied crop and excludes its surround", "[robust][viewer]") {
    auto pixels = negativeRamp(256, 128);
    auto params = fullAnalysisRegion();
    params.cropEnable = true;
    params.imageCropMaxX = 0.5f;
    REQUIRE(imageViewCoordinates::fitAnalysisRegionToView(params, 2.0f));
    const auto result = meter(pixels, 256, 128, params);
    REQUIRE(result.valid);
    CHECK(result.white[0] < 0.56f);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 128; x < 256; ++x)
            for (int ch = 0; ch < 3; ++ch) pixels[(y * 256 + x) * 4 + ch] = 0.0f;
    const auto surrounded = meter(pixels, 256, 128, params);
    REQUIRE(surrounded.valid);
    CHECK_THAT(surrounded.white[0], WithinAbs(result.white[0], 1e-6f));
    CHECK(result.minX < 0.5f);
    CHECK(result.maxX < 0.5f);
}

TEST_CASE("Robust analysis rejects empty, flat and non-finite regions", "[robust]") {
    auto params = fullAnalysisRegion();
    auto pixels = negativeRamp(32, 32);
    std::fill(pixels.begin(), pixels.end(), 0.5f);
    CHECK_FALSE(meter(pixels, 32, 32, params).valid);
    std::fill(pixels.begin(), pixels.end(), std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(meter(pixels, 32, 32, params).valid);
    std::fill(params.cropBoxX, params.cropBoxX + 4, 0.5f);
    CHECK_FALSE(meter(pixels, 32, 32, params).valid);
    const float base[] = {1, 1, 1};
    CHECK_FALSE(robustAnalysis::analyze(nullptr, 32, 32, base, params.cropBoxX, params.cropBoxY).valid);
}

TEST_CASE("Robust analysis respects a skewed region rather than its bounding rectangle", "[robust]") {
    auto pixels = negativeRamp(128, 128);
    auto params = fullAnalysisRegion();
    const float x[] = {0.5f, 1.0f, 0.5f, 0.0f}, y[] = {0.0f, 0.5f, 1.0f, 0.5f};
    std::copy(x, x + 4, params.cropBoxX);
    std::copy(y, y + 4, params.cropBoxY);
    const auto clean = meter(pixels, 128, 128, params);
    REQUIRE(clean.valid);
    for (unsigned row = 0; row < 128; ++row)
        for (unsigned col = 0; col < 128; ++col)
            if (!robustAnalysis::contains((col + 0.5f) / 128, (row + 0.5f) / 128, x, y))
                for (int ch = 0; ch < 3; ++ch) pixels[(row * 128 + col) * 4 + ch] = 0.0f;
    const auto surrounded = meter(pixels, 128, 128, params);
    REQUIRE(surrounded.valid);
    CHECK_THAT(surrounded.black[0], WithinAbs(clean.black[0], 1e-6f));
    CHECK_THAT(surrounded.white[0], WithinAbs(clean.white[0], 1e-6f));
}

TEST_CASE("Robust analysis uses the sampled film base in the existing inversion space", "[robust]") {
    auto pixels = negativeRamp(128, 128);
    const auto params = fullAnalysisRegion();
    const float base[] = {0.8f, 0.5f, 0.3f};
    for (std::size_t i = 0; i < pixels.size() / 4; ++i)
        for (int ch = 0; ch < 3; ++ch) pixels[i * 4 + ch] *= base[ch];
    const auto result = robustAnalysis::analyze(pixels.data(), 128, 128, base, params.cropBoxX, params.cropBoxY);
    REQUIRE(result.valid);
    for (int ch = 1; ch < 3; ++ch) {
        CHECK_THAT(result.black[ch], WithinAbs(result.black[0], 1e-6f));
        CHECK_THAT(result.white[ch], WithinAbs(result.white[0], 1e-6f));
    }
    const float badBase[] = {0, 0.5f, 0.3f};
    CHECK_FALSE(robustAnalysis::analyze(pixels.data(), 128, 128, badBase, params.cropBoxX, params.cropBoxY).valid);
}

TEST_CASE("Robust sampling stays bounded and stable across scan resolutions", "[robust]") {
    const auto params = fullAnalysisRegion();
    const auto preview = meter(negativeRamp(320, 240), 320, 240, params);
    const auto full = meter(negativeRamp(1280, 960), 1280, 960, params);
    REQUIRE(preview.valid);
    REQUIRE(full.valid);
    CHECK(full.sampleCount <= 128 * 128);
    CHECK(full.sampleCount == preview.sampleCount);
    CHECK_THAT(full.black[0], WithinAbs(preview.black[0], 0.005f));
    CHECK_THAT(full.white[0], WithinAbs(preview.white[0], 0.005f));
}

TEST_CASE("Robust analysis preference round-trips and old preferences retain legacy analysis", "[robust]") {
    preferenceSet prefs;
    CHECK_FALSE(prefs.robustAnalysis);
    prefs.robustAnalysis = true;
    nlohmann::json saved = prefs;
    CHECK(saved.get<preferenceSet>().robustAnalysis);
    saved.erase("robustAnalysis");
    CHECK_FALSE(saved.get<preferenceSet>().robustAnalysis);
}

TEST_CASE("Compare robust analysis with the existing full-frame blur", "[robust-benchmark][.]") {
    auto pixels = negativeRamp(3000, 2000);
    const auto params = fullAnalysisRegion();
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto result = meter(pixels, 3000, 2000, params);
    const auto robustEnd = Clock::now();
    REQUIRE(result.valid);
    image img;
    img.width = img.rndrW = 3000;
    img.height = img.rndrH = 2000;
    img.nChannels = 4;
    img.rawImgData = pixels.data();
    img.allocBlurBuf();
    img.blurImage();
    const auto legacyEnd = Clock::now();
    img.delBlurBuf();
    img.rawImgData = nullptr;
    std::cout << "3000x2000 analysis: robust "
              << std::chrono::duration<double, std::milli>(robustEnd - start).count()
              << " ms; legacy blur alone "
              << std::chrono::duration<double, std::milli>(legacyEnd - robustEnd).count()
              << " ms; samples " << result.sampleCount << '\n';
}

// Forward declarations — these functions are defined in imageProcessing.cpp
// which is compiled into the test binary via IMAGE_SRCS.
float  evalCurve(float v, const float* curve, int n);
float4 JPLogtoLin(float4 inputPixel);
float4 LintoJPLog(float4 inputPixel);
float4 sampleImageMitchell(const float* rawImgData, int inputWidth, int inputHeight,
                           float u, float v);
void   getCroppedRotatedUV(float u, float v, float& outU, float& outV,
                           const renderParams& imgParam,
                           int inputWidth, int inputHeight,
                           int outputWidth, int outputHeight);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build an interleaved curve array from separate px/py arrays of length n.
// Layout: out[i*2] = px[i], out[i*2+1] = py[i]
static void makeCurve(float* out, const float* px, const float* py, int n) {
    for (int i = 0; i < n; ++i) {
        out[i * 2]     = px[i];
        out[i * 2 + 1] = py[i];
    }
}

// Convenience: evaluate a curve defined by raw px/py arrays of length n.
static float eval(const float* px, const float* py, int n, float v) {
    float buf[CURVE_MAX_PTS * 2];
    makeCurve(buf, px, py, n);
    return evalCurve(v, buf, n);
}

// ---------------------------------------------------------------------------
// Named test fixtures
// ---------------------------------------------------------------------------

// y = x identity — 5 default points
static constexpr int kN5 = 5;
static const float kIdentityPx[kN5] = {0.00f, 0.25f, 0.50f, 0.75f, 1.00f};
static const float kIdentityPy[kN5] = {0.00f, 0.25f, 0.50f, 0.75f, 1.00f};

// S-curve (5 points): compressed shadows & highlights, symmetric around 0.5
static const float kSCurvePx[kN5] = {0.00f, 0.25f, 0.50f, 0.75f, 1.00f};
static const float kSCurvePy[kN5] = {0.00f, 0.10f, 0.50f, 0.90f, 1.00f};

// 3-point contrast boost: straight line through (0,0), lifted midpoint, (1,1)
static constexpr int kN3 = 3;
static const float k3Px[kN3] = {0.00f, 0.50f, 1.00f};
static const float k3Py[kN3] = {0.00f, 0.70f, 1.00f};

// 7-point fine-control S-curve: more control points than the default 5
static constexpr int kN7 = 7;
static const float k7Px[kN7] = {0.00f, 0.15f, 0.30f, 0.50f, 0.70f, 0.85f, 1.00f};
static const float k7Py[kN7] = {0.00f, 0.05f, 0.20f, 0.50f, 0.80f, 0.95f, 1.00f};

// Build a zeroed renderParams and fill only the fields we care about
static renderParams makeBaseParams(int w = 100, int h = 100) {
    renderParams p{};
    p.cropEnable        = 0;
    p.cropVisible       = 0;
    p.imageCropMinX     = 0.0f;
    p.imageCropMinY     = 0.0f;
    p.imageCropMaxX     = 1.0f;
    p.imageCropMaxY     = 1.0f;
    p.arbitraryRotation = 0.0f;
    p.width             = static_cast<unsigned int>(w);
    p.height            = static_cast<unsigned int>(h);
    return p;
}

// Fill an RGBA image buffer with a constant colour
static std::vector<float> makeConstantImage(int w, int h,
                                            float r, float g, float b, float a = 1.0f) {
    std::vector<float> img(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        img[i * 4 + 0] = r;
        img[i * 4 + 1] = g;
        img[i * 4 + 2] = b;
        img[i * 4 + 3] = a;
    }
    return img;
}

// ---------------------------------------------------------------------------
// evalCurve — 5-point curves (original default behaviour)
// ---------------------------------------------------------------------------
TEST_CASE("evalCurve: identity curve maps every value to itself", "[evalCurve]") {
    for (float v : {0.0f, 0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f, 1.0f})
        REQUIRE_THAT(eval(kIdentityPx, kIdentityPy, kN5, v), WithinAbs(v, 1e-5f));
}

TEST_CASE("evalCurve: returns exact py value at each control point", "[evalCurve]") {
    const float px[5] = {0.00f, 0.25f, 0.50f, 0.75f, 1.00f};
    const float py[5] = {0.00f, 0.10f, 0.50f, 0.90f, 1.00f};
    for (int i = 0; i < 5; ++i)
        REQUIRE_THAT(eval(px, py, 5, px[i]), WithinAbs(py[i], 1e-5f));
}

TEST_CASE("evalCurve: S-curve (5pt) is monotonically non-decreasing across [0,1]", "[evalCurve]") {
    float prev = eval(kSCurvePx, kSCurvePy, kN5, 0.0f);
    for (int i = 1; i <= 200; ++i) {
        float v   = i * 0.005f;
        float cur = eval(kSCurvePx, kSCurvePy, kN5, v);
        CHECK(cur >= prev - 1e-5f);
        prev = cur;
    }
}

TEST_CASE("evalCurve: extrapolates linearly below the black point", "[evalCurve]") {
    // Identity entry slope = 1 → evalCurve(-t) ≈ -t
    REQUIRE_THAT(eval(kIdentityPx, kIdentityPy, kN5, -0.10f), WithinAbs(-0.10f, 1e-4f));
    REQUIRE_THAT(eval(kIdentityPx, kIdentityPy, kN5, -0.50f), WithinAbs(-0.50f, 1e-4f));
}

TEST_CASE("evalCurve: extrapolates linearly above the white point", "[evalCurve]") {
    REQUIRE_THAT(eval(kIdentityPx, kIdentityPy, kN5, 1.10f), WithinAbs(1.10f, 1e-4f));
    REQUIRE_THAT(eval(kIdentityPx, kIdentityPy, kN5, 1.50f), WithinAbs(1.50f, 1e-4f));
}

TEST_CASE("evalCurve: flat curve returns constant value throughout", "[evalCurve]") {
    const float px[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float py[5] = {0.5f, 0.5f,  0.5f, 0.5f,  0.5f};
    for (float v : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f})
        REQUIRE_THAT(eval(px, py, 5, v), WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("evalCurve: coincident X control points do not produce NaN or Inf", "[evalCurve]") {
    // px[0] == px[1] forces dx < 0.0001 — should return pay, not divide by zero
    const float px[5] = {0.0f, 0.0f, 0.5f, 0.75f, 1.0f};
    const float py[5] = {0.0f, 0.3f, 0.5f, 0.75f, 1.0f};
    float result = eval(px, py, 5, 0.0f);
    CHECK(std::isfinite(result));
}

TEST_CASE("evalCurve: endpoint extrapolation slope matches S-curve boundary chords", "[evalCurve]") {
    // Entry slope: (py1-py0)/(px1-px0) = (0.1-0.0)/(0.25-0.0) = 0.4
    // evalCurve(-0.25) ≈ 0.0 + 0.4*(-0.25) = -0.1
    REQUIRE_THAT(eval(kSCurvePx, kSCurvePy, kN5, -0.25f), WithinAbs(-0.10f, 1e-4f));
    // Exit slope: (py4-py3)/(px4-px3) = (1.0-0.9)/(1.0-0.75) = 0.4
    // evalCurve(1.25) ≈ 1.0 + 0.4*0.25 = 1.1
    REQUIRE_THAT(eval(kSCurvePx, kSCurvePy, kN5, 1.25f), WithinAbs(1.10f, 1e-4f));
}

// ---------------------------------------------------------------------------
// evalCurve — 3-point curves
// ---------------------------------------------------------------------------
TEST_CASE("evalCurve: 3-point curve passes through all control points", "[evalCurve][custom-pts]") {
    for (int i = 0; i < kN3; ++i)
        REQUIRE_THAT(eval(k3Px, k3Py, kN3, k3Px[i]), WithinAbs(k3Py[i], 1e-5f));
}

TEST_CASE("evalCurve: 3-point contrast boost is monotonically non-decreasing", "[evalCurve][custom-pts]") {
    float prev = eval(k3Px, k3Py, kN3, 0.0f);
    for (int i = 1; i <= 100; ++i) {
        float v   = (float)i / 100.0f;
        float cur = eval(k3Px, k3Py, kN3, v);
        CHECK(cur >= prev - 1e-5f);
        prev = cur;
    }
}

TEST_CASE("evalCurve: 3-point curve output is always >= input (contrast boost)", "[evalCurve][custom-pts]") {
    // k3Py lifts the midpoint, so outputs in (0,1) should be above identity
    for (int i = 1; i < 20; ++i) {
        float v   = (float)i / 20.0f;
        float out = eval(k3Px, k3Py, kN3, v);
        // Not strictly true at endpoints (both are 0 and 1), check interior
        if (v > 0.05f && v < 0.95f)
            CHECK(out >= v - 1e-5f);
    }
}

TEST_CASE("evalCurve: 3-point linear extrapolation below and above", "[evalCurve][custom-pts]") {
    // Entry slope: (py1-py0)/(px1-px0) = (0.7-0.0)/(0.5-0.0) = 1.4
    REQUIRE_THAT(eval(k3Px, k3Py, kN3, -0.1f), WithinAbs(-0.14f, 1e-4f));
    // Exit slope: (py2-py1)/(px2-px1) = (1.0-0.7)/(1.0-0.5) = 0.6
    REQUIRE_THAT(eval(k3Px, k3Py, kN3, 1.1f),  WithinAbs(1.06f, 1e-4f));
}

// ---------------------------------------------------------------------------
// evalCurve — 7-point curves
// ---------------------------------------------------------------------------
TEST_CASE("evalCurve: 7-point curve passes through all control points", "[evalCurve][custom-pts]") {
    for (int i = 0; i < kN7; ++i)
        REQUIRE_THAT(eval(k7Px, k7Py, kN7, k7Px[i]), WithinAbs(k7Py[i], 1e-5f));
}

TEST_CASE("evalCurve: 7-point S-curve is monotonically non-decreasing across [0,1]", "[evalCurve][custom-pts]") {
    float prev = eval(k7Px, k7Py, kN7, 0.0f);
    for (int i = 1; i <= 200; ++i) {
        float v   = i * 0.005f;
        float cur = eval(k7Px, k7Py, kN7, v);
        CHECK(cur >= prev - 1e-5f);
        prev = cur;
    }
}

TEST_CASE("evalCurve: 7-point curve output is bounded in [0,1] for inputs in [0,1]", "[evalCurve][custom-pts]") {
    for (int i = 0; i <= 100; ++i) {
        float v   = (float)i / 100.0f;
        float out = eval(k7Px, k7Py, kN7, v);
        CHECK(out >= -1e-5f);
        CHECK(out <=  1.0f + 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// evalCurve — adding a midpoint does not shift existing curve values
// ---------------------------------------------------------------------------
TEST_CASE("evalCurve: inserting a point on the curve does not change surrounding values",
          "[evalCurve][custom-pts]") {
    // Start with 5-point identity. Evaluate at x=0.35 — this value lies on the curve.
    float ref = eval(kIdentityPx, kIdentityPy, kN5, 0.35f);

    // Insert (0.35, ref) as a 6th point into the curve, keeping sorted order.
    // New point list: 0, 0.25, 0.35, 0.50, 0.75, 1.0
    const int kN6 = 6;
    const float px6[kN6] = {0.00f, 0.25f, 0.35f, 0.50f, 0.75f, 1.00f};
    const float py6[kN6] = {0.00f, 0.25f, ref,   0.50f, 0.75f, 1.00f};

    // Values at and around the inserted point should be unchanged.
    REQUIRE_THAT(eval(px6, py6, kN6, 0.35f), WithinAbs(ref,   1e-4f));
    REQUIRE_THAT(eval(px6, py6, kN6, 0.25f), WithinAbs(0.25f, 1e-4f));
    REQUIRE_THAT(eval(px6, py6, kN6, 0.50f), WithinAbs(0.50f, 1e-4f));

    // The 6-point curve should still be monotone.
    float prev = eval(px6, py6, kN6, 0.0f);
    for (int i = 1; i <= 100; ++i) {
        float v   = (float)i / 100.0f;
        float cur = eval(px6, py6, kN6, v);
        CHECK(cur >= prev - 1e-5f);
        prev = cur;
    }
}

TEST_CASE("evalCurve: 5-point and 7-point versions agree at shared control points",
          "[evalCurve][custom-pts]") {
    // The 7-point S-curve and 5-point S-curve share (0,0), (0.5,0.5), and (1,1).
    // They should agree exactly at those anchors.
    REQUIRE_THAT(eval(k7Px, k7Py, kN7, 0.0f), WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(eval(k7Px, k7Py, kN7, 0.5f), WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(eval(k7Px, k7Py, kN7, 1.0f), WithinAbs(1.0f, 1e-5f));
}

// ---------------------------------------------------------------------------
// JPLogtoLin / LintoJPLog
// ---------------------------------------------------------------------------

TEST_CASE("JPLog: Lin→Log→Lin round-trip in the linear segment", "[jplog]") {
    // Values at or below ALOGSM1_LIN_BRKPNT (0.006801...) use the linear branch.
    for (float x : {0.0f, 0.001f, 0.004f, 0.006801176276f}) {
        float4 px      = {x, x, x, 1.0f};
        float4 log_px  = LintoJPLog(px);
        float4 lin_px  = JPLogtoLin(log_px);
        REQUIRE_THAT(lin_px.x, WithinAbs(x, 1e-6f));
        REQUIRE_THAT(lin_px.y, WithinAbs(x, 1e-6f));
        REQUIRE_THAT(lin_px.z, WithinAbs(x, 1e-6f));
    }
}

TEST_CASE("JPLog: Lin→Log→Lin round-trip in the log segment", "[jplog]") {
    for (float x : {0.01f, 0.1f, 0.5f, 1.0f, 2.0f}) {
        float4 px      = {x, x, x, 1.0f};
        float4 log_px  = LintoJPLog(px);
        float4 lin_px  = JPLogtoLin(log_px);
        REQUIRE_THAT(lin_px.x, WithinRel(x, 1e-5f));
    }
}

TEST_CASE("JPLog: Log→Lin→Log round-trip", "[jplog]") {
    const float LOG_BRKPNT = 0.16129032258064516129f;
    for (float y : {0.05f, LOG_BRKPNT, 0.3f, 0.5f, 0.8f}) {
        float4 py        = {y, y, y, 1.0f};
        float4 lin_py    = JPLogtoLin(py);
        float4 roundtrip = LintoJPLog(lin_py);
        REQUIRE_THAT(roundtrip.x, WithinAbs(y, 1e-5f));
    }
}

TEST_CASE("JPLog: transform is continuous at the breakpoint", "[jplog]") {
    const float LIN_BRKPNT = 0.006801176276f;
    float4 just_below = {LIN_BRKPNT * 0.99f, LIN_BRKPNT * 0.99f, LIN_BRKPNT * 0.99f, 1.0f};
    float4 just_above = {LIN_BRKPNT * 1.01f, LIN_BRKPNT * 1.01f, LIN_BRKPNT * 1.01f, 1.0f};
    float4 log_below  = LintoJPLog(just_below);
    float4 log_above  = LintoJPLog(just_above);
    REQUIRE_THAT(log_above.x - log_below.x, WithinAbs(0.0f, 0.005f));
}

TEST_CASE("JPLog: LintoJPLog returns finite values for typical inputs", "[jplog]") {
    for (float x : {0.0f, 0.001f, 0.1f, 0.5f, 1.0f}) {
        float4 result = LintoJPLog({x, x, x, 1.0f});
        CHECK(std::isfinite(result.x));
        CHECK(std::isfinite(result.y));
        CHECK(std::isfinite(result.z));
    }
}

// ---------------------------------------------------------------------------
// sampleImageMitchell
// ---------------------------------------------------------------------------

TEST_CASE("sampleImageMitchell: constant image returns that constant at any UV", "[mitchell]") {
    auto img = makeConstantImage(8, 8, 0.5f, 0.25f, 0.75f);
    float4 result = sampleImageMitchell(img.data(), 8, 8, 0.5f, 0.5f);
    REQUIRE_THAT(result.x, WithinAbs(0.50f, 1e-5f));
    REQUIRE_THAT(result.y, WithinAbs(0.25f, 1e-5f));
    REQUIRE_THAT(result.z, WithinAbs(0.75f, 1e-5f));
}

TEST_CASE("sampleImageMitchell: constant image is invariant across all UV positions", "[mitchell]") {
    auto img = makeConstantImage(8, 8, 0.4f, 0.4f, 0.4f);
    for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        for (float v : {0.0f, 0.5f, 1.0f}) {
            float4 result = sampleImageMitchell(img.data(), 8, 8, u, v);
            REQUIRE_THAT(result.x, WithinAbs(0.4f, 1e-5f));
        }
    }
}

TEST_CASE("sampleImageMitchell: UV outside [0,1] clamps without NaN or Inf", "[mitchell]") {
    auto img = makeConstantImage(8, 8, 0.5f, 0.5f, 0.5f);
    for (auto [u, v] : std::initializer_list<std::pair<float,float>>{
            {-0.5f, 0.5f}, {1.5f, 0.5f}, {0.5f, -0.5f}, {0.5f, 1.5f}}) {
        float4 r = sampleImageMitchell(img.data(), 8, 8, u, v);
        CHECK(std::isfinite(r.x));
        CHECK(std::isfinite(r.y));
        CHECK(std::isfinite(r.z));
    }
}

TEST_CASE("sampleImageMitchell: result alpha is always 1.0", "[mitchell]") {
    auto img   = makeConstantImage(4, 4, 0.3f, 0.6f, 0.9f, 0.5f);
    float4 res = sampleImageMitchell(img.data(), 4, 4, 0.5f, 0.5f);
    CHECK(res.w == 1.0f);
}

// ---------------------------------------------------------------------------
// getCroppedRotatedUV
// ---------------------------------------------------------------------------

TEST_CASE("getCroppedRotatedUV: no crop and no rotation is a pass-through", "[cropRotUV]") {
    renderParams p = makeBaseParams();
    float outU, outV;
    getCroppedRotatedUV(0.3f, 0.7f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(outV, WithinAbs(0.7f, 1e-6f));
}

TEST_CASE("getCroppedRotatedUV: center crop maps corners to crop boundaries", "[cropRotUV]") {
    renderParams p = makeBaseParams();
    p.cropEnable    = 1;
    p.imageCropMinX = 0.25f;  p.imageCropMinY = 0.25f;
    p.imageCropMaxX = 0.75f;  p.imageCropMaxY = 0.75f;

    float outU, outV;

    getCroppedRotatedUV(0.0f, 0.0f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.25f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(0.25f, 1e-5f));

    getCroppedRotatedUV(0.5f, 0.5f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.50f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(0.50f, 1e-5f));

    getCroppedRotatedUV(1.0f, 1.0f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.75f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(0.75f, 1e-5f));
}

TEST_CASE("getCroppedRotatedUV: zero rotation with square image is identity", "[cropRotUV]") {
    renderParams p = makeBaseParams();
    p.cropVisible       = 1;
    p.arbitraryRotation = 0.0f;

    float outU, outV;
    for (float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        getCroppedRotatedUV(u, 0.5f, outU, outV, p, 100, 100, 100, 100);
        REQUIRE_THAT(outU, WithinAbs(u,    1e-5f));
        REQUIRE_THAT(outV, WithinAbs(0.5f, 1e-5f));
    }
}

TEST_CASE("getCroppedRotatedUV: 90-degree rotation on a square image", "[cropRotUV]") {
    renderParams p = makeBaseParams();
    p.cropVisible       = 1;
    p.arbitraryRotation = static_cast<float>(M_PI / 2.0);

    float outU, outV;

    getCroppedRotatedUV(0.5f, 0.5f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(0.5f, 1e-5f));

    getCroppedRotatedUV(1.0f, 0.5f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(1.0f, 1e-5f));

    getCroppedRotatedUV(0.5f, 0.0f, outU, outV, p, 100, 100, 100, 100);
    REQUIRE_THAT(outU, WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(outV, WithinAbs(0.5f, 1e-5f));
}
