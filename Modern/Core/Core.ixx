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
export module recastnav.core;
import std;

export namespace recastnav {
enum class Error : std::uint32_t {
    InvalidLimits, InvalidProfile, InvalidBounds, InvalidIndex, InvalidArea,
    InvalidTopology, NonFiniteGeometry, DegenerateGeometry, NonConvexPolygon,
    InvalidDetailMesh, InvalidOffMeshConnection, InvalidLayer, BudgetExceeded,
    SizeOverflow, AllocationFailure, TruncatedInput, InvalidMagic,
    UnsupportedVersion, InvalidEncoding, ChecksumMismatch, TrailingData,
};
class Failure final {
public:
    constexpr explicit Failure(Error error, std::size_t offset = 0,
        std::optional<std::size_t> element = {}) noexcept : m_error{error}, m_offset{offset}, m_element{element} {}
    [[nodiscard]] constexpr Error GetError() const noexcept { return m_error; }
    [[nodiscard]] constexpr std::size_t GetOffset() const noexcept { return m_offset; }
    [[nodiscard]] constexpr const std::optional<std::size_t>& GetElement() const noexcept { return m_element; }
private:
    Error m_error;
    std::size_t m_offset{};
    std::optional<std::size_t> m_element{};
};
template<class Tag> class Index final {
public:
    constexpr explicit Index(std::size_t value = 0) noexcept : m_value{value} {}
    [[nodiscard]] constexpr std::size_t GetValue() const noexcept { return m_value; }
    [[nodiscard]] constexpr auto operator<=>(const Index&) const noexcept = default;
private:
    std::size_t m_value{};
};
struct VertexIndexTag;
struct PolygonIndexTag;
struct DetailVertexIndexTag;
using VertexIndex = Index<VertexIndexTag>;
using PolygonIndex = Index<PolygonIndexTag>;
using DetailVertexIndex = Index<DetailVertexIndexTag>;

class DocumentLimits final {
public:
    constexpr DocumentLimits(std::size_t bytes = 256u * 1024u * 1024u,
        std::size_t vertices = 1'048'576u, std::size_t polygons = 1'048'576u,
        std::size_t polygonVertices = 6u, std::size_t detailVertices = 4'194'304u,
        std::size_t detailTriangles = 4'194'304u, std::size_t offMeshConnections = 1'048'576u,
        std::size_t layerCells = 4'194'304u, std::size_t categories = 64u) noexcept
        : m_bytes{bytes}, m_vertices{vertices}, m_polygons{polygons}, m_polygonVertices{polygonVertices},
          m_detailVertices{detailVertices}, m_detailTriangles{detailTriangles}, m_offMesh{offMeshConnections},
          m_layerCells{layerCells}, m_categories{categories} {}
    [[nodiscard]] constexpr std::size_t GetMaximumBytes() const noexcept { return m_bytes; }
    [[nodiscard]] constexpr std::size_t GetMaximumVertices() const noexcept { return m_vertices; }
    [[nodiscard]] constexpr std::size_t GetMaximumPolygons() const noexcept { return m_polygons; }
    [[nodiscard]] constexpr std::size_t GetMaximumPolygonVertices() const noexcept { return m_polygonVertices; }
    [[nodiscard]] constexpr std::size_t GetMaximumDetailVertices() const noexcept { return m_detailVertices; }
    [[nodiscard]] constexpr std::size_t GetMaximumDetailTriangles() const noexcept { return m_detailTriangles; }
    [[nodiscard]] constexpr std::size_t GetMaximumOffMeshConnections() const noexcept { return m_offMesh; }
    [[nodiscard]] constexpr std::size_t GetMaximumLayerCells() const noexcept { return m_layerCells; }
    [[nodiscard]] constexpr std::size_t GetMaximumCategories() const noexcept { return m_categories; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return m_bytes >= 32u && m_polygonVertices >= 3u && m_polygonVertices <= 6u; }
private:
    std::size_t m_bytes{}, m_vertices{}, m_polygons{}, m_polygonVertices{};
    std::size_t m_detailVertices{}, m_detailTriangles{}, m_offMesh{}, m_layerCells{}, m_categories{};
};
[[nodiscard]] constexpr std::expected<std::size_t, Failure> CheckedProduct(std::size_t count,
    std::size_t width, std::size_t limit) noexcept {
    if (width != 0 && count > limit / width) { return std::unexpected{Failure{Error::SizeOverflow}}; }
    return count * width;
}
}
