#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace imageViewCoordinates {
struct Point {
    float x;
    float y;
};

// Normalized coordinates, using the same EXIF orientation as the viewer quad.
inline Point orient(Point p, int rotation) {
    switch (rotation) {
        case 2: return {1.0f - p.x, p.y};
        case 3: return {1.0f - p.x, 1.0f - p.y};
        case 4: return {p.x, 1.0f - p.y};
        case 5: return {1.0f - p.y, 1.0f - p.x};
        case 6: return {1.0f - p.y, p.x};
        case 7: return {p.y, p.x};
        case 8: return {p.y, 1.0f - p.x};
        default: return p;
    }
}

inline Point rotate(Point p, float degrees, float aspect) {
    const float radians = degrees * 0.017453292519943295f;
    const float c = std::cos(radians), s = std::sin(radians);
    const float x = (p.x - 0.5f) * aspect, y = p.y - 0.5f;
    return {(x * c - y * s) / aspect + 0.5f, x * s + y * c + 0.5f};
}

template <typename Params>
bool sourceToViewUnclipped(Point source, const Params& params, float aspect, Point& view) {
    Point p = source;
    if (params.cropEnable) {
        // Undo the shader's rotation about the full image center before
        // expressing the point relative to the crop, then apply EXIF orientation.
        p = rotate(p, -params.arbitraryRotation, aspect);
        const float w = params.imageCropMaxX - params.imageCropMinX;
        const float h = params.imageCropMaxY - params.imageCropMinY;
        if (w <= 0.0f || h <= 0.0f) return false;
        p = {(p.x - params.imageCropMinX) / w, (p.y - params.imageCropMinY) / h};
    }
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    view = orient(p, params.rotation);
    return true;
}

template <typename Params>
bool sourceToView(Point source, const Params& params, float aspect, Point& view) {
    return sourceToViewUnclipped(source, params, aspect, view) &&
           view.x >= 0.0f && view.x <= 1.0f && view.y >= 0.0f && view.y <= 1.0f;
}

// Intersect the analysis outline with the displayed image. Simply discarding
// off-crop corners would hide regions that enclose or cross the entire crop.
inline std::vector<Point> clipPolygonToView(std::vector<Point> polygon) {
    for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
        const bool horizontal = edge >= 2;
        const float boundary = edge % 2 == 0 ? 0.0f : 1.0f;
        auto coordinate = [horizontal](Point p) { return horizontal ? p.y : p.x; };
        auto inside = [&](Point p) {
            return edge % 2 == 0 ? coordinate(p) >= boundary : coordinate(p) <= boundary;
        };
        std::vector<Point> clipped;
        Point previous = polygon.back();
        for (Point current : polygon) {
            if (inside(previous) != inside(current)) {
                const float t = (boundary - coordinate(previous)) /
                                (coordinate(current) - coordinate(previous));
                Point intersection = {previous.x + t * (current.x - previous.x),
                                      previous.y + t * (current.y - previous.y)};
                if (horizontal) intersection.y = boundary;
                else intersection.x = boundary;
                clipped.push_back(intersection);
            }
            if (inside(current)) clipped.push_back(current);
            previous = current;
        }
        polygon = std::move(clipped);
    }
    return polygon;
}

template <typename Params>
Point viewToSource(Point view, const Params& params, float aspect) {
    // Keep dragging at the visible image edge when the pointer leaves it.
    view.x = std::clamp(view.x, 0.0f, 1.0f);
    view.y = std::clamp(view.y, 0.0f, 1.0f);
    const int inverseRotation = params.rotation == 6 ? 8 : params.rotation == 8 ? 6 : params.rotation;
    Point p = orient(view, inverseRotation);
    if (params.cropEnable) {
        p = {params.imageCropMinX + p.x * (params.imageCropMaxX - params.imageCropMinX),
             params.imageCropMinY + p.y * (params.imageCropMaxY - params.imageCropMinY)};
        p = rotate(p, params.arbitraryRotation, aspect);
    }
    // Rotated crops may include background outside the source image.
    return {std::clamp(p.x, 0.0f, 1.0f), std::clamp(p.y, 0.0f, 1.0f)};
}
} // namespace imageViewCoordinates
