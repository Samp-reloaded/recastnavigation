// Copyright (c) 2026 Samp-reloaded contributors.
// Altered implementation; distributed under the repository's zlib license.
#include <doctest/doctest.h>
import std;
import recastnav.document;
import recastnav.build;

namespace {
using namespace recastnav;
[[nodiscard]] Triangle Face(std::size_t a, std::size_t b, std::size_t c, std::uint32_t area = 63u) {
    return Triangle{std::array{VertexIndex{a}, VertexIndex{b}, VertexIndex{c}}, area};
}
class Scene final {
public:
    Scene() : m_vertices{{0, 0, 0}, {0, 0, 12}, {12, 0, 12}, {12, 0, 0}},
        m_triangles{Face(0, 1, 2), Face(0, 2, 3)} {}
    void AddLowObstacle() {
        m_vertices.insert(m_vertices.end(), {{4, 0, 4}, {4, 0, 8}, {8, 0, 8}, {8, 0, 4},
            {4, 1.5f, 4}, {4, 1.5f, 8}, {8, 1.5f, 8}, {8, 1.5f, 4}});
        for (const auto face : std::array{Face(8, 9, 10, 0), Face(8, 10, 11, 0),
            Face(4, 8, 11, 0), Face(4, 11, 7, 0), Face(5, 9, 8, 0), Face(5, 8, 4, 0),
            Face(6, 10, 9, 0), Face(6, 9, 5, 0), Face(7, 11, 10, 0), Face(7, 10, 6, 0)}) {
            m_triangles.push_back(face);
        }
    }
    void MakeSteep() { m_vertices[2] = Vec3{12, 24, 12}; m_vertices[3] = Vec3{12, 24, 0}; }
    void AddInvalidFace() { m_triangles.push_back(Face(0, 1, 99)); }
    [[nodiscard]] BuildInput Input(BuildProfile profile = {0.3f, 0.2f, 2.0f, 0.6f, 0.3f, 45.0f}) const {
        return {m_vertices, m_triangles, {{0, 0, 0}, {12, 25, 12}}, profile, {3, -9, 2}};
    }
private:
    std::vector<Vec3> m_vertices;
    std::vector<Triangle> m_triangles;
};
[[nodiscard]] double ProjectedArea(const TileDocument& document) {
    double result{};
    for (const auto& polygon : document.GetPolygons()) {
        const auto indices{polygon.GetVertices()}; const auto& a{document.GetVertices()[indices[0].GetValue()]};
        for (std::size_t i{1}; i + 1u < indices.size(); ++i) {
            result += UpwardTurn(a, document.GetVertices()[indices[i].GetValue()], document.GetVertices()[indices[i + 1u].GetValue()]) / 2.0;
        }
    }
    return result;
}
class CountingResource final : public std::pmr::memory_resource {
public:
    explicit CountingResource(std::optional<std::size_t> failAt = {}) : m_failAt{failAt} {}
    [[nodiscard]] std::size_t GetOutstandingBytes() const noexcept { return m_outstanding; }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++m_allocations;
        if (m_failAt && m_allocations == *m_failAt) { throw std::bad_alloc{}; }
        void* memory{std::pmr::new_delete_resource()->allocate(bytes, alignment)};
        m_outstanding += bytes;
        return memory;
    }
    void do_deallocate(void* memory, std::size_t bytes, std::size_t alignment) override {
        m_outstanding -= bytes; std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    std::optional<std::size_t> m_failAt;
    std::size_t m_allocations{}, m_outstanding{};
};
}

TEST_CASE("Cooking a Y-up floor generates immutable polygons and detail geometry deterministically") {
    Scene scene{}; CountingResource scratch{}; BuildContext context{scratch};
    auto first{CookTile(scene.Input(), context)}; REQUIRE(first.has_value());
    CHECK_FALSE(first->GetPolygons().empty()); CHECK(first->GetDetails().size() == first->GetPolygons().size());
    CHECK(ProjectedArea(*first) > 80.0); CHECK(ProjectedArea(*first) < 144.0);
    const TileKey expectedKey{3, -9, 2}; CHECK(first->GetMetadata().GetKey() == expectedKey);
    CHECK(scratch.GetOutstandingBytes() == 0u);
    auto second{CookTile(scene.Input(), context)}; REQUIRE(second.has_value()); CHECK(*first == *second);
    CHECK(scratch.GetOutstandingBytes() == 0u);
}
TEST_CASE("Rasterized low obstacle removes floor clearance and agent-radius erosion expands the exclusion") {
    Scene open{}, blocked{}; blocked.AddLowObstacle(); CountingResource scratch{}; BuildContext context{scratch};
    auto floor{CookTile(open.Input(), context)}, obstacle{CookTile(blocked.Input(), context)};
    REQUIRE(floor.has_value()); REQUIRE(obstacle.has_value()); CHECK_FALSE(obstacle->GetPolygons().empty());
    CHECK(ProjectedArea(*obstacle) < ProjectedArea(*floor) - 16.0);
    CHECK(scratch.GetOutstandingBytes() == 0u);
}
TEST_CASE("Triangle slopes are classified against Y-up and the configured maximum slope") {
    Scene scene{}; scene.MakeSteep(); CountingResource scratch{}; BuildContext context{scratch};
    auto rejected{CookTile(scene.Input(), context)}; REQUIRE(rejected.has_value()); CHECK(rejected->GetPolygons().empty());
    auto allowed{CookTile(scene.Input({0.3f, 0.2f, 2.0f, 0.0f, 2.0f, 75.0f}), context)};
    REQUIRE(allowed.has_value()); CHECK_FALSE(allowed->GetPolygons().empty());
    CHECK(scratch.GetOutstandingBytes() == 0u);
}
TEST_CASE("Invalid indices fail before allocation with a typed source failure") {
    Scene scene{}; scene.AddInvalidFace(); CountingResource scratch{}; BuildContext context{scratch};
    auto result{CookTile(scene.Input(), context)}; REQUIRE_FALSE(result.has_value());
    CHECK(result.error().GetError() == BuildError::InvalidInput);
    REQUIRE(result.error().GetSourceFailure().has_value()); CHECK(result.error().GetSourceFailure()->GetError() == Error::InvalidIndex);
    CHECK(scratch.GetOutstandingBytes() == 0u);
}
TEST_CASE("Cancellation and cooking scratch budgets return typed failures without publication") {
    Scene scene{}; CountingResource scratch{}; std::stop_source stop{}; stop.request_stop();
    BuildContext cancelled{scratch, stop.get_token()}; auto stopped{CookTile(scene.Input(), cancelled)};
    REQUIRE_FALSE(stopped.has_value()); CHECK(stopped.error().GetError() == BuildError::Cancelled);
    BuildContext bounded{scratch, {}, 16u}; auto tooLarge{CookTile(scene.Input(), bounded)};
    REQUIRE_FALSE(tooLarge.has_value()); CHECK(tooLarge.error().GetError() == BuildError::BudgetExceeded);
    CHECK(scratch.GetOutstandingBytes() == 0u);
}
TEST_CASE("Injected resource failures roll back upstream raw allocations across cooking stages") {
    Scene scene{}; scene.AddLowObstacle();
    for (std::size_t failAt{1}; failAt <= 64u; ++failAt) {
        CountingResource scratch{failAt}; BuildContext context{scratch};
        const auto result{CookTile(scene.Input(), context)};
        if (!result) { CHECK(result.error().GetError() == BuildError::AllocationFailure); }
        CHECK(scratch.GetOutstandingBytes() == 0u);
    }
}
