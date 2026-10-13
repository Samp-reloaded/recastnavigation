// Copyright (c) 2009 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Copyright (c) 2026 Samp-reloaded contributors.
// This altered implementation is part of the Samp-reloaded fork.
#pragma once

// Included after import std and the owning module declaration. These helpers
// have translation-unit linkage and share no state between snapshots or queries.
namespace recastnav {
namespace {
using DoublePoint = std::array<double, 3>;
[[nodiscard]] DoublePoint Coordinates(const Vec3& p) noexcept { return {p.GetX(), p.GetY(), p.GetZ()}; }
[[nodiscard]] DoublePoint Difference(const DoublePoint& a, const DoublePoint& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
[[nodiscard]] double Dot(const DoublePoint& a, const DoublePoint& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
[[nodiscard]] double DistanceSquared(const Vec3& a, const Vec3& b) noexcept {
    const auto d{Difference(Coordinates(a), Coordinates(b))}; return Dot(d, d);
}
[[nodiscard]] double Distance(const Vec3& a, const Vec3& b) noexcept { return std::sqrt(DistanceSquared(a, b)); }
[[nodiscard]] Vec3 Interpolate(const Vec3& a, const Vec3& b, double t) noexcept {
    const auto aa{Coordinates(a)}, bb{Coordinates(b)};
    const auto coordinate = [&](std::size_t i) {
        return static_cast<float>(std::clamp(aa[i] + (bb[i] - aa[i]) * t, std::min(aa[i], bb[i]), std::max(aa[i], bb[i])));
    };
    return {coordinate(0), coordinate(1), coordinate(2)};
}
[[nodiscard]] Vec3 InterpolateTriangle(const Vec3& a, const Vec3& b, const Vec3& c, double v, double w) noexcept {
    const auto aa{Coordinates(a)}, bb{Coordinates(b)}, cc{Coordinates(c)};
    const auto coordinate = [&](std::size_t i) {
        return static_cast<float>(std::clamp(aa[i] + (bb[i] - aa[i]) * v + (cc[i] - aa[i]) * w,
            std::min({aa[i], bb[i], cc[i]}), std::max({aa[i], bb[i], cc[i]})));
    };
    return {coordinate(0), coordinate(1), coordinate(2)};
}
[[nodiscard]] Vec3 ClosestOnTriangle(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    const auto aa{Coordinates(a)}, bb{Coordinates(b)}, cc{Coordinates(c)}, pp{Coordinates(point)};
    const auto ab{Difference(bb, aa)}, ac{Difference(cc, aa)};
    const DoublePoint normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
    std::size_t dominant{};
    for (std::size_t i{1}; i < 3u; ++i) { if (std::abs(normal[i]) > std::abs(normal[dominant])) { dominant = i; } }
    const double scale{std::abs(normal[dominant])};
    const DoublePoint unit{normal[0] / scale, normal[1] / scale, normal[2] / scale};
    const double planeOffset{Dot(Difference(pp, aa), unit) / Dot(unit, unit)};
    const DoublePoint projected{pp[0] - unit[0] * planeOffset, pp[1] - unit[1] * planeOffset, pp[2] - unit[2] * planeOffset};
    const auto ap{Difference(projected, aa)};
    const std::size_t i{(dominant + 1u) % 3u}, j{(dominant + 2u) % 3u};
    const double denominator{ab[i] * ac[j] - ab[j] * ac[i]};
    const double v{(ap[i] * ac[j] - ap[j] * ac[i]) / denominator};
    const double w{(ab[i] * ap[j] - ab[j] * ap[i]) / denominator};
    if (v >= 0.0 && w >= 0.0 && v + w <= 1.0) { return InterpolateTriangle(a, b, c, v, w); }
    Vec3 best{a}; double bestDistance{DistanceSquared(point, a)};
    const auto segment = [&](const Vec3& from, const Vec3& to) {
        const auto edge{Difference(Coordinates(to), Coordinates(from))};
        const double length{Dot(edge, edge)};
        const double t{std::clamp(Dot(Difference(pp, Coordinates(from)), edge) / length, 0.0, 1.0)};
        const auto candidate{Interpolate(from, to, t)}; const double distance{DistanceSquared(point, candidate)};
        if (distance < bestDistance) { best = candidate; bestDistance = distance; }
    };
    segment(a, b); segment(b, c); segment(c, a); return best;
}
template<class Visitor>
    requires std::invocable<const Visitor&, const Vec3&, const Vec3&, const Vec3&>
void ForEachSurfaceTriangle(const TileDocument& tile, PolygonIndex polygonIndex, const Visitor& visit) noexcept {
    const auto polygon{tile.GetPolygons()[polygonIndex.GetValue()].GetVertices()};
    const auto vertices{tile.GetVertices()};
    if (tile.GetDetails().empty()) {
        for (std::size_t i{1}; i + 1u < polygon.size(); ++i) {
            const auto& a{vertices[polygon[0].GetValue()]};
            const auto& b{vertices[polygon[i].GetValue()]}; const auto& c{vertices[polygon[i + 1u].GetValue()]};
            // Convex polygons may contain redundant collinear boundary vertices.
            if (UpwardTurn(a, b, c) > 0.0) { visit(a, b, c); }
        }
    } else {
        const auto& detail{tile.GetDetails()[polygonIndex.GetValue()]};
        const auto vertex = [&](DetailVertexIndex index) -> const Vec3& {
            return index.GetValue() < polygon.size() ? vertices[polygon[index.GetValue()].GetValue()]
                : detail.GetAdditionalVertices()[index.GetValue() - polygon.size()];
        };
        for (const auto& triangle : detail.GetTriangles()) {
            const auto& indices{triangle.GetVertices()}; visit(vertex(indices[0]), vertex(indices[1]), vertex(indices[2]));
        }
    }
}
[[nodiscard]] Vec3 ClosestSurface(const TileDocument& tile, PolygonIndex polygonIndex, const Vec3& point) noexcept {
    const auto polygon{tile.GetPolygons()[polygonIndex.GetValue()].GetVertices()};
    Vec3 closest{tile.GetVertices()[polygon[0].GetValue()]};
    double best{DistanceSquared(point, closest)};
    ForEachSurfaceTriangle(tile, polygonIndex, [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const auto candidate{ClosestOnTriangle(point, a, b, c)};
        const double distance{DistanceSquared(point, candidate)};
        if (distance < best) { closest = candidate; best = distance; }
    });
    return closest;
}
[[nodiscard]] double HorizontalDistanceSquared(const Vec3& a, const Vec3& b) noexcept {
    const double x{static_cast<double>(a.GetX()) - b.GetX()}, z{static_cast<double>(a.GetZ()) - b.GetZ()};
    return x * x + z * z;
}
[[nodiscard]] Vec3 ClosestOnTriangleXZ(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    if (UpwardTurn(a, b, point) >= 0.0 && UpwardTurn(b, c, point) >= 0.0 && UpwardTurn(c, a, point) >= 0.0) {
        const double denominator{UpwardTurn(a, b, c)};
        return InterpolateTriangle(a, b, c, UpwardTurn(c, a, point) / denominator, UpwardTurn(a, b, point) / denominator);
    }
    Vec3 best{a}; double bestDistance{HorizontalDistanceSquared(point, a)};
    const auto segment = [&](const Vec3& from, const Vec3& to) {
        const double dx{static_cast<double>(to.GetX()) - from.GetX()}, dz{static_cast<double>(to.GetZ()) - from.GetZ()};
        const double length{dx * dx + dz * dz};
        if (length == 0.0) { return; }
        const double px{static_cast<double>(point.GetX()) - from.GetX()}, pz{static_cast<double>(point.GetZ()) - from.GetZ()};
        const auto candidate{Interpolate(from, to, std::clamp((px * dx + pz * dz) / length, 0.0, 1.0))};
        const double distance{HorizontalDistanceSquared(point, candidate)};
        if (distance < bestDistance) { best = candidate; bestDistance = distance; }
    };
    segment(a, b); segment(b, c); segment(c, a); return best;
}
[[nodiscard]] Vec3 ClosestSurfaceXZ(const TileDocument& tile, PolygonIndex polygonIndex, const Vec3& point) noexcept {
    const auto polygon{tile.GetPolygons()[polygonIndex.GetValue()].GetVertices()};
    Vec3 closest{tile.GetVertices()[polygon[0].GetValue()]};
    double best{HorizontalDistanceSquared(point, closest)}, bestVertical{std::abs(static_cast<double>(point.GetY()) - closest.GetY())};
    ForEachSurfaceTriangle(tile, polygonIndex, [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const auto candidate{ClosestOnTriangleXZ(point, a, b, c)};
        const double distance{HorizontalDistanceSquared(point, candidate)}, vertical{std::abs(static_cast<double>(point.GetY()) - candidate.GetY())};
        if (distance < best || (distance == best && vertical < bestVertical)) {
            closest = candidate; best = distance; bestVertical = vertical;
        }
    });
    return closest;
}
[[nodiscard]] Aabb PolygonBounds(const TileDocument& tile, PolygonIndex polygonIndex) noexcept {
    const auto polygon{tile.GetPolygons()[polygonIndex.GetValue()].GetVertices()};
    auto low{Coordinates(tile.GetVertices()[polygon[0].GetValue()])}, high{low};
    const auto include = [&](const Vec3& point) {
        const auto p{Coordinates(point)};
        for (std::size_t i{}; i < 3u; ++i) { low[i] = std::min(low[i], p[i]); high[i] = std::max(high[i], p[i]); }
    };
    for (const auto index : polygon) { include(tile.GetVertices()[index.GetValue()]); }
    if (!tile.GetDetails().empty()) {
        for (const auto& point : tile.GetDetails()[polygonIndex.GetValue()].GetAdditionalVertices()) { include(point); }
    }
    return {{static_cast<float>(low[0]), static_cast<float>(low[1]), static_cast<float>(low[2])},
        {static_cast<float>(high[0]), static_cast<float>(high[1]), static_cast<float>(high[2])}};
}
[[nodiscard]] Vec3 PolygonCentre(const TileDocument& tile, PolygonIndex polygonIndex) noexcept {
    DoublePoint sum{}; const auto indices{tile.GetPolygons()[polygonIndex.GetValue()].GetVertices()};
    for (const auto index : indices) {
        const auto p{Coordinates(tile.GetVertices()[index.GetValue()])};
        for (std::size_t i{}; i < 3u; ++i) { sum[i] += p[i]; }
    }
    const double count{static_cast<double>(indices.size())};
    const Vec3 average{static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count), static_cast<float>(sum[2] / count)};
    return ClosestSurface(tile, polygonIndex, average);
}
[[nodiscard]] bool IntersectsSearch(const Aabb& bounds, const Vec3& position, const Vec3& extents) noexcept {
    const auto low{Coordinates(bounds.GetMinimum())}, high{Coordinates(bounds.GetMaximum())};
    const auto p{Coordinates(position)}, e{Coordinates(extents)};
    for (std::size_t i{}; i < 3u; ++i) { if (low[i] > p[i] + e[i] || high[i] < p[i] - e[i]) { return false; } }
    return true;
}
}
}
