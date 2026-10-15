#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "imageParams.h"
#include "window/imageViewCoordinates.h"

using Catch::Matchers::WithinAbs;
using namespace imageViewCoordinates;

TEST_CASE("Apply Crop follows crop width with a top and bottom border inset", "[viewer][crop][analysis]") {
    for (int rotation = 1; rotation <= 8; ++rotation) {
        for (float angle : {0.0f, 15.0f, -15.0f}) {
            CAPTURE(rotation, angle);
            imageParams params;
            params.cropEnable = true;
            params.rotation = rotation;
            params.arbitraryRotation = angle;
            params.imageCropMinX = 0.3f;
            params.imageCropMaxX = 0.7f;
            params.imageCropMinY = 0.25f;
            params.imageCropMaxY = 0.65f;
            // A custom zone wholly inside the crop must expand with it too.
            for (int i = 0; i < 4; ++i) {
                params.cropBoxX[i] = 0.45f;
                params.cropBoxY[i] = 0.45f;
            }
            alignAnalysisRegionToCrop(params, 1.5f);
            const Point corners[] = {{0.0f, 0.0f}, {1.0f, 0.0f},
                                     {1.0f, 1.0f}, {0.0f, 1.0f}};
            for (int i = 0; i < 4; ++i) {
                Point view;
                REQUIRE(sourceToViewUnclipped({params.cropBoxX[i], params.cropBoxY[i]},
                                             params, 1.5f, view));
                Point expected = orient(corners[i], rotation);
                expected.y = expected.y == 0.0f ? 0.02f : 0.98f;
                CHECK_THAT(view.x, WithinAbs(expected.x, 1e-6f));
                CHECK_THAT(view.y, WithinAbs(expected.y, 1e-6f));
            }
            CHECK_FALSE(fitAnalysisRegionToView(params, 1.5f));
        }
    }
}

TEST_CASE("Roll crops align analysis for each image without copying color or orientation", "[viewer][crop][analysis]") {
    imageParams source;
    source.imageCropMinX = 0.3f;
    source.imageCropMaxX = 0.7f;
    source.imageCropMinY = 0.25f;
    source.imageCropMaxY = 0.65f;
    source.arbitraryRotation = 12.0f;
    source.imageCropAspect = 1.5f;
    source.lockAspect = true;
    source.cropEnable = false; // Also applies directly from the editing overlay.
    source.rotation = 1;
    source.temp = 0.7f;
    for (float aspect : {0.75f, 1.0f, 1.5f}) {
        imageParams target;
        target.rotation = 6;
        target.temp = -0.2f;
        target.cropVisible = true;
        applyCropFrom(target, source, aspect);
        CHECK(target.cropEnable);
        CHECK_FALSE(target.cropVisible);
        CHECK(target.rotation == 6);
        CHECK(target.temp == -0.2f);
        CHECK(target.lockAspect);
        CHECK(target.imageCropAspect == 1.5f);
        const Point corners[] = {{0.0f, 0.0f}, {1.0f, 0.0f},
                                 {1.0f, 1.0f}, {0.0f, 1.0f}};
        for (int i = 0; i < 4; ++i) {
            Point view;
            REQUIRE(sourceToViewUnclipped({target.cropBoxX[i], target.cropBoxY[i]}, target, aspect, view));
            auto expected = orient(corners[i], target.rotation);
            expected.y = expected.y == 0.0f ? 0.02f : 0.98f;
            CHECK_THAT(view.x, WithinAbs(expected.x, 1e-6f));
            CHECK_THAT(view.y, WithinAbs(expected.y, 1e-6f));
        }
    }
    CHECK_FALSE(source.cropEnable);
}

TEST_CASE("Reapplying a moved crop updates both analysis edges without accumulating inset", "[viewer][crop][analysis]") {
    imageParams params;
    params.cropEnable = true;
    params.rotation = 1;
    for (const auto bounds : {std::array<float, 2>{0.1f, 0.9f},
                              std::array<float, 2>{0.3f, 0.6f}}) {
        params.imageCropMinX = 0.2f;
        params.imageCropMaxX = 0.8f;
        params.imageCropMinY = bounds[0];
        params.imageCropMaxY = bounds[1];
        for (int repeat = 0; repeat < 2; ++repeat) {
            alignAnalysisRegionToCrop(params, 1.5f);
            for (int i = 0; i < 4; ++i) {
                CHECK_THAT(params.cropBoxX[i], WithinAbs(i == 0 || i == 3 ? 0.2f : 0.8f, 1e-6f));
                const float y = bounds[0] + (i < 2 ? 0.02f : 0.98f) * (bounds[1] - bounds[0]);
                CHECK_THAT(params.cropBoxY[i], WithinAbs(y, 1e-6f));
            }
        }
    }
}

TEST_CASE("Post-crop analysis corners match their displayed handles before dragging", "[viewer][crop][analysis][drag]") {
    for (int rotation = 1; rotation <= 8; ++rotation) {
        CAPTURE(rotation);
        imageParams params;
        params.cropEnable = true;
        params.rotation = rotation;
        params.imageCropMinX = params.imageCropMinY = 0.2f;
        params.imageCropMaxX = params.imageCropMaxY = 0.8f;
        // Reproduce the screenshot: three original corners lie outside the
        // crop and the bottom-right corner has been pulled into the image.
        const std::array<Point, 4> original = {{{0.1f, 0.1f}, {0.9f, 0.1f},
                                               {0.764f, 0.608f}, {0.1f, 0.9f}}};
        for (int i = 0; i < 4; ++i) {
            params.cropBoxX[i] = original[i].x;
            params.cropBoxY[i] = original[i].y;
        }
        REQUIRE(fitAnalysisRegionToView(params, 1.0f));
        const std::array<Point, 4> expected = {{{0.0f, 0.0f}, {1.0f, 0.0f},
                                               {0.94f, 0.68f}, {0.0f, 1.0f}}};
        std::vector<Point> outline;
        for (int i = 0; i < 4; ++i) {
            Point view;
            REQUIRE(sourceToViewUnclipped({params.cropBoxX[i], params.cropBoxY[i]}, params, 1.0f, view));
            const Point handle = orient(expected[i], rotation);
            CHECK_THAT(view.x, WithinAbs(handle.x, 1e-6f));
            CHECK_THAT(view.y, WithinAbs(handle.y, 1e-6f));
            const auto source = viewToSource(view, params, 1.0f);
            CHECK_THAT(source.x, WithinAbs(params.cropBoxX[i], 1e-6f));
            CHECK_THAT(source.y, WithinAbs(params.cropBoxY[i], 1e-6f));
            outline.push_back(view);
        }
        // No hidden fifth corner can form the extra bend along the bottom.
        CHECK(clipPolygonToView(outline).size() == 4);
        CHECK_FALSE(fitAnalysisRegionToView(params, 1.0f));
    }
}

TEST_CASE("Analysis corners fit rotated crops without drifting on subsequent frames", "[viewer][crop][analysis][drag]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = params.imageCropMinY = 0.3f;
    params.imageCropMaxX = params.imageCropMaxY = 0.7f;
    params.arbitraryRotation = 15.0f;
    const std::array<Point, 4> corners = {{{0.1f, 0.1f}, {0.9f, 0.1f},
                                          {0.9f, 0.9f}, {0.1f, 0.9f}}};
    for (int i = 0; i < 4; ++i) {
        params.cropBoxX[i] = corners[i].x;
        params.cropBoxY[i] = corners[i].y;
    }
    REQUIRE(fitAnalysisRegionToView(params, 1.5f));
    for (int i = 0; i < 4; ++i) {
        Point view;
        REQUIRE(sourceToViewUnclipped({params.cropBoxX[i], params.cropBoxY[i]}, params, 1.5f, view));
        CHECK(view.x >= -1e-6f);
        CHECK(view.x <= 1.0f + 1e-6f);
        CHECK(view.y >= -1e-6f);
        CHECK(view.y <= 1.0f + 1e-6f);
    }
    CHECK_FALSE(fitAnalysisRegionToView(params, 1.5f));
}

TEST_CASE("Analysis corner dragging follows valid motion without moving other corners", "[viewer][analysis][drag]") {
    const std::array<Point, 4> corners = {{{0.1f, 0.1f}, {0.9f, 0.1f},
                                          {0.9f, 0.9f}, {0.1f, 0.9f}}};
    const auto moved = constrainAnalysisCorner(corners, 0, {0.3f, 0.2f});
    CHECK_THAT(moved.x, WithinAbs(0.3f, 1e-6f));
    CHECK_THAT(moved.y, WithinAbs(0.2f, 1e-6f));
    CHECK(corners[1].x == 0.9f);
    CHECK(corners[1].y == 0.1f);
    const auto edge = constrainAnalysisCorner(corners, 0, {-0.2f, -0.4f});
    CHECK(edge.x == 0.0f);
    CHECK(edge.y == 0.0f);
}

TEST_CASE("Analysis corners stop before folding or crossing the outline", "[viewer][analysis][drag]") {
    for (bool reversed : {false, true}) {
        std::array<Point, 4> corners = {{{0.1f, 0.1f}, {0.9f, 0.1f},
                                        {0.9f, 0.9f}, {0.1f, 0.9f}}};
        if (reversed) std::reverse(corners.begin(), corners.end());
        for (int corner = 0; corner < 4; ++corner) {
            CAPTURE(reversed, corner);
            auto polygon = corners;
            polygon[corner] = constrainAnalysisCorner(corners, corner, corners[(corner + 2) % 4]);
            // The corner moves toward the pointer but stops before the opposite diagonal.
            CHECK_THAT(polygon[corner].x, WithinAbs(0.5f, 1e-5f));
            CHECK_THAT(polygon[corner].y, WithinAbs(0.5f, 1e-5f));
            for (int i = 0; i < 4; ++i) {
                const auto a = polygon[i], b = polygon[(i + 1) % 4], c = polygon[(i + 2) % 4];
                const float turn = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                CHECK(turn * (reversed ? -1.0f : 1.0f) > 0.0f);
            }
            const auto outline = clipPolygonToView({polygon.begin(), polygon.end()});
            CHECK(outline.size() == 4);
        }
    }
}

TEST_CASE("Applied crops retain visible min/max positions", "[viewer][crop]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = 0.2f;
    params.imageCropMaxX = 0.8f;
    params.imageCropMinY = 0.1f;
    params.imageCropMaxY = 0.5f;

    Point view;
    REQUIRE(sourceToView({0.35f, 0.4f}, params, 1.5f, view));
    CHECK_THAT(view.x, WithinAbs(0.25f, 1e-6f));
    CHECK_THAT(view.y, WithinAbs(0.75f, 1e-6f));
    CHECK_FALSE(sourceToView({0.9f, 0.4f}, params, 1.5f, view));
    // An invisible max point must not prevent the min point from being shown.
    CHECK(sourceToView({0.35f, 0.4f}, params, 1.5f, view));

    params.cropEnable = false;
    REQUIRE(sourceToView({0.9f, 0.4f}, params, 1.5f, view));
    CHECK_THAT(view.x, WithinAbs(0.9f, 1e-6f));
}

TEST_CASE("Crop point mapping follows every EXIF viewer orientation", "[viewer][crop]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = 0.2f;
    params.imageCropMaxX = 0.8f;
    params.imageCropMinY = 0.1f;
    params.imageCropMaxY = 0.5f;
    // Source point maps to crop UV (0.25, 0.75). Expected screen UV comes
    // from the image quad's texture coordinates, including mirrored rotations.
    const Point expected[] = {{0.25f, 0.75f}, {0.75f, 0.75f},
        {0.75f, 0.25f}, {0.25f, 0.25f}, {0.25f, 0.75f},
        {0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}};
    for (int rotation = 1; rotation <= 8; ++rotation) {
        CAPTURE(rotation);
        params.rotation = rotation;
        Point view;
        REQUIRE(sourceToView({0.35f, 0.4f}, params, 1.5f, view));
        CHECK_THAT(view.x, WithinAbs(expected[rotation - 1].x, 1e-6f));
        CHECK_THAT(view.y, WithinAbs(expected[rotation - 1].y, 1e-6f));
        const auto source = viewToSource(expected[rotation - 1], params, 1.5f);
        CHECK_THAT(source.x, WithinAbs(0.35f, 1e-6f));
        CHECK_THAT(source.y, WithinAbs(0.4f, 1e-6f));
    }
}

TEST_CASE("Rotated crop dots follow the shader's source pixels", "[viewer][crop]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = 0.2f;
    params.imageCropMaxX = 0.8f;
    params.imageCropMinY = 0.1f;
    params.imageCropMaxY = 0.5f;
    params.arbitraryRotation = 90.0f;
    // Crop UV (0.25, 0.75) gives full-image UV (0.35, 0.4).
    // The shader rotates about (0.5, 0.5) with aspect ratio 2,
    // producing the source pixel (0.55, 0.2), rather than rotating
    // about the crop center or rotating in stretched crop coordinates.
    for (int rotation = 1; rotation <= 8; ++rotation) {
        CAPTURE(rotation);
        params.rotation = rotation;
        const Point screen = orient({0.25f, 0.75f}, rotation);
        const auto source = viewToSource(screen, params, 2.0f);
        CHECK_THAT(source.x, WithinAbs(0.55f, 1e-6f));
        CHECK_THAT(source.y, WithinAbs(0.2f, 1e-6f));
        Point view;
        REQUIRE(sourceToView({0.55f, 0.2f}, params, 2.0f, view));
        CHECK_THAT(view.x, WithinAbs(screen.x, 1e-6f));
        CHECK_THAT(view.y, WithinAbs(screen.y, 1e-6f));
    }
}

TEST_CASE("Min/max dragging stops at visible crop edges", "[viewer][crop]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = 0.2f;
    params.imageCropMaxX = 0.8f;
    params.imageCropMinY = 0.1f;
    params.imageCropMaxY = 0.5f;
    const auto source = viewToSource({-0.5f, 1.5f}, params, 1.5f);
    CHECK_THAT(source.x, WithinAbs(0.2f, 1e-6f));
    CHECK_THAT(source.y, WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("Image edge dots are valid and empty crops are rejected", "[viewer][crop]") {
    imageParams params;
    Point view;
    REQUIRE(sourceToView({0.0f, 0.0f}, params, 1.5f, view));
    CHECK(view.x == 0.0f);
    CHECK(view.y == 0.0f);
    params.cropEnable = true;
    params.imageCropMaxX = params.imageCropMinX;
    CHECK_FALSE(sourceToView({0.0f, 0.0f}, params, 1.5f, view));
}

TEST_CASE("Analysis regions enclosing a crop remain visible in every orientation", "[viewer][crop][analysis]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = params.imageCropMinY = 0.25f;
    params.imageCropMaxX = params.imageCropMaxY = 0.75f;
    const Point sourceCorners[] = {{0.1f, 0.1f}, {0.9f, 0.1f},
                                   {0.9f, 0.9f}, {0.1f, 0.9f}};
    for (int rotation = 1; rotation <= 8; ++rotation) {
        CAPTURE(rotation);
        params.rotation = rotation;
        std::vector<Point> corners;
        for (Point source : sourceCorners) {
            Point view;
            CHECK_FALSE(sourceToView(source, params, 1.0f, view));
            REQUIRE(sourceToViewUnclipped(source, params, 1.0f, view));
            corners.push_back(view);
        }
        const auto outline = clipPolygonToView(corners);
        REQUIRE(outline.size() == 4);
        for (Point p : outline) {
            CHECK((p.x == 0.0f || p.x == 1.0f));
            CHECK((p.y == 0.0f || p.y == 1.0f));
        }
    }
}

TEST_CASE("Analysis edges crossing a crop are drawn when every corner is outside", "[viewer][crop][analysis]") {
    const auto outline = clipPolygonToView({{-0.5f, 0.4f}, {1.5f, 0.4f},
                                            {1.5f, 0.6f}, {-0.5f, 0.6f}});
    REQUIRE(outline.size() == 4);
    for (Point p : outline) {
        CHECK((p.x == 0.0f || p.x == 1.0f));
        CHECK((p.y == 0.4f || p.y == 0.6f));
    }
}

TEST_CASE("Rotated analysis regions clip to all four crop edges", "[viewer][crop][analysis]") {
    imageParams params;
    params.cropEnable = true;
    params.imageCropMinX = params.imageCropMinY = 0.2f;
    params.imageCropMaxX = params.imageCropMaxY = 0.8f;
    params.arbitraryRotation = 45.0f;
    std::vector<Point> corners;
    for (Point source : std::vector<Point>{{0.2f, 0.2f}, {0.8f, 0.2f},
                                           {0.8f, 0.8f}, {0.2f, 0.8f}}) {
        Point view;
        REQUIRE(sourceToViewUnclipped(source, params, 1.0f, view));
        corners.push_back(view);
    }
    const auto outline = clipPolygonToView(corners);
    REQUIRE(outline.size() == 8);
    for (Point p : outline) {
        CHECK(p.x >= 0.0f);
        CHECK(p.x <= 1.0f);
        CHECK(p.y >= 0.0f);
        CHECK(p.y <= 1.0f);
        CHECK((p.x == 0.0f || p.x == 1.0f || p.y == 0.0f || p.y == 1.0f));
    }
}

TEST_CASE("Analysis regions outside the visible crop have no outline", "[viewer][crop][analysis]") {
    CHECK(clipPolygonToView({{-1.0f, 0.2f}, {-0.5f, 0.2f},
                            {-0.5f, 0.8f}, {-1.0f, 0.8f}}).empty());
}
