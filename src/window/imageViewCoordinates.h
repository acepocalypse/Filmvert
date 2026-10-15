#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace imageViewCoordinates {
struct Point {
    float x;
    float y;
};

// Keep the four analysis corners in perimeter order. A freely dragged corner
// can otherwise turn the region into a concave or self-intersecting polygon.
inline Point constrainAnalysisCorner(const std::array<Point, 4>& corners,
                                     int corner, Point target) {
    if (corner < 0 || corner >= 4) return target;
    target.x = std::clamp(target.x, 0.0f, 1.0f);
    target.y = std::clamp(target.y, 0.0f, 1.0f);
    float area = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const auto a = corners[i], b = corners[(i + 1) % 4];
        area += a.x * b.y - a.y * b.x;
    }
    const float winding = area >= 0.0f ? 1.0f : -1.0f;
    auto valid = [&](Point candidate) {
        auto polygon = corners;
        polygon[corner] = candidate;
        for (int i = 0; i < 4; ++i) {
            const auto a = polygon[i], b = polygon[(i + 1) % 4],
                       c = polygon[(i + 2) % 4];
            const float turn = (b.x - a.x) * (c.y - b.y) -
                               (b.y - a.y) * (c.x - b.x);
            if (!std::isfinite(turn) || turn * winding <= 1e-6f) return false;
        }
        return true;
    };
    if (valid(target)) return target;
    const Point start = corners[corner];
    if (!valid(start)) return start;
    // Stop at the nearest edge rather than rejecting the entire mouse motion.
    float low = 0.0f, high = 1.0f;
    for (int i = 0; i < 24; ++i) {
        const float t = (low + high) * 0.5f;
        const Point p = {start.x + t * (target.x - start.x),
                         start.y + t * (target.y - start.y)};
        if (valid(p)) low = t;
        else high = t;
    }
    return {start.x + low * (target.x - start.x),
            start.y + low * (target.y - start.y)};
}

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

// Follow the applied crop at full width, excluding a small border at the
// displayed top and bottom. Store source coordinates for the sampled pixels.
template <typename Params>
void alignAnalysisRegionToCrop(Params& params, float aspect) {
    if (!params.cropEnable) return;
    constexpr float borderInset = 0.02f;
    const Point corners[] = {{0.0f, 0.0f}, {1.0f, 0.0f},
                             {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        Point view = orient(corners[i], params.rotation);
        view.y = borderInset + view.y * (1.0f - 2.0f * borderInset);
        const Point source = viewToSource(view, params, aspect);
        params.cropBoxX[i] = source.x;
        params.cropBoxY[i] = source.y;
    }
}

// Share normalized crop settings while preserving each image's orientation
// and color adjustments. Recompute the analysis zone for its own dimensions.
template <typename Params>
void applyCropFrom(Params& target, const Params& source, float aspect) {
    target.imageCropMinX = source.imageCropMinX;
    target.imageCropMaxX = source.imageCropMaxX;
    target.imageCropMinY = source.imageCropMinY;
    target.imageCropMaxY = source.imageCropMaxY;
    target.arbitraryRotation = source.arbitraryRotation;
    target.imageCropAspect = source.imageCropAspect;
    target.lockAspect = source.lockAspect;
    target.cropEnable = true;
    target.cropVisible = false;
    alignAnalysisRegionToCrop(target, aspect);
}

// After applying a crop, the editable corners must be the actual source
// positions represented by the visible handles, rather than hidden points
// outside the crop with independently clamped handles.
template <typename Params>
bool fitAnalysisRegionToView(Params& params, float aspect) {
    if (!params.cropEnable) return false;
    std::array<Point, 4> fitted;
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        Point view;
        const Point source = {params.cropBoxX[i], params.cropBoxY[i]};
        if (!sourceToViewUnclipped(source, params, aspect, view)) return false;
        // Avoid repeatedly rewriting points for floating-point roundoff.
        if (view.x < -1e-5f || view.x > 1.0f + 1e-5f ||
            view.y < -1e-5f || view.y > 1.0f + 1e-5f) {
            fitted[i] = viewToSource(view, params, aspect);
            changed = true;
        } else {
            fitted[i] = source;
        }
    }
    if (changed) {
        for (int i = 0; i < 4; ++i) {
            params.cropBoxX[i] = fitted[i].x;
            params.cropBoxY[i] = fitted[i].y;
        }
    }
    return changed;
}
} // namespace imageViewCoordinates
