#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "imageParams.h"
#include "window/imageViewCoordinates.h"

using Catch::Matchers::WithinAbs;
using namespace imageViewCoordinates;

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
