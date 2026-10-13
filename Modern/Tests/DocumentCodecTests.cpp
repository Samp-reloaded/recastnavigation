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
#include <doctest/doctest.h>
import std;
import recastnav.document;
import recastnav.codec.tile;
import recastnav.codec.layer;

namespace {
using namespace recastnav;
[[nodiscard]] TileMetadata Metadata() {
    CookFingerprint fingerprint{}; fingerprint[0] = std::byte{0xab}; fingerprint[31] = std::byte{0x78};
    return {{-4, 8, 257u}, {{0.0f, -1.0f, -1.0f}, {1.0f, 2.0f, 0.0f}}, {}, fingerprint};
}
[[nodiscard]] std::expected<TileDocument, Failure> Tile() {
    std::vector<Vec3> vertices{{0,0,0},{1,0,0},{1,0,-1},{0,0,-1}};
    std::vector<Polygon> polygons{};
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0},VertexIndex{1},VertexIndex{2},VertexIndex{3}},
        std::vector<EdgeConnection>{PortalSide::PositiveZ,PortalSide::PositiveX,PortalSide::NegativeZ,PortalSide::NegativeX},
        63u, std::vector<std::uint32_t>{2u,1'000'000u});
    std::vector<DetailTriangle> triangles{};
    triangles.emplace_back(std::array{DetailVertexIndex{0},DetailVertexIndex{1},DetailVertexIndex{2}}, std::array{true,true,false});
    triangles.emplace_back(std::array{DetailVertexIndex{0},DetailVertexIndex{2},DetailVertexIndex{3}}, std::array{false,true,true});
    std::vector<DetailMesh> details{}; details.emplace_back(std::vector<Vec3>{},std::move(triangles));
    std::vector<OffMeshConnection> links{};
    links.emplace_back(Vec3{0.5f,0,-0.5f}, Vec3{2,1,-0.5f},0.2f,std::numeric_limits<std::uint64_t>::max(),63u,
        std::vector<std::uint32_t>{65'536u},true);
    return TileDocument::Create(Metadata(),std::move(vertices),std::move(polygons),std::move(details),std::move(links));
}
[[nodiscard]] std::expected<LayerDocument, Failure> Layer() {
    std::array<LayerConnection,4> first{}, second{};
    first[0] = LayerConnection{false,true}; first[2] = LayerConnection{true,false};
    second[0] = LayerConnection{true,false}; second[2] = LayerConnection{false,true};
    std::vector<LayerCell> cells{LayerCell{300u,63u,first},LayerCell{301u,7u,second}};
    return LayerDocument::Create(Metadata(),2u,1u,{0u,0u,1u,0u},400u,800u,std::move(cells));
}
[[nodiscard]] std::expected<TileDocument, Failure> WithDetail(std::vector<DetailTriangle> triangles,
    std::vector<Vec3> additional = {}) {
    const auto source{Tile()};
    if (!source) { return std::unexpected{source.error()}; }
    std::vector<DetailMesh> details{}; details.emplace_back(std::move(additional), std::move(triangles));
    return TileDocument::Create(source->GetMetadata(),
        std::vector<Vec3>{source->GetVertices().begin(), source->GetVertices().end()},
        std::vector<Polygon>{source->GetPolygons().begin(), source->GetPolygons().end()}, std::move(details));
}
void Put64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i{};i<8u;++i) { bytes[offset+i]=static_cast<std::byte>(static_cast<std::uint8_t>(value>>(8u*i))); }
}
void Rehash(std::vector<std::byte>& bytes) {
    std::uint64_t hash{14695981039346656037ull};
    for (const auto value : std::span<const std::byte>{bytes}.subspan(32u)) { hash^=std::to_integer<std::uint8_t>(value);hash*=1099511628211ull; }
    Put64(bytes,16u,hash);
}
}

TEST_CASE("SRNT field-wise tile roundtrip retains geometry, detail, attributes and wide metadata") {
    auto source{Tile()}; REQUIRE(source.has_value());
    auto bytes{recastnav::codec::EncodeTile(*source)}; REQUIRE(bytes.has_value());
    CHECK((*bytes)[0] == std::byte{'S'}); CHECK((*bytes)[1] == std::byte{'R'});
    auto result{recastnav::codec::DecodeTile(*bytes)}; REQUIRE(result.has_value()); CHECK(*result == *source);
    auto second{recastnav::codec::EncodeTile(*result)}; REQUIRE(second.has_value()); CHECK(*second == *bytes);
    CHECK(result->GetMetadata().GetKey().GetLayer() == 257u);
    CHECK(result->GetOffMeshConnections()[0].GetUserId() == std::numeric_limits<std::uint64_t>::max());
}
TEST_CASE("SRNL roundtrip preserves heights beyond upstream byte encoding and ordinary connections") {
    auto source{Layer()}; REQUIRE(source.has_value()); auto bytes{recastnav::codec::EncodeLayer(*source)}; REQUIRE(bytes.has_value());
    auto result{recastnav::codec::DecodeLayer(*bytes)}; REQUIRE(result.has_value()); CHECK(*result == *source);
    REQUIRE(result->GetCells()[0].GetHeight().has_value()); CHECK(*result->GetCells()[0].GetHeight() == 300u);
    CHECK(result->GetCells()[0].GetConnections()[0].IsPortal()); CHECK(result->GetCells()[0].GetConnections()[2].HasNeighbour());
}
TEST_CASE("Every truncated SRNT and SRNL packet is refused") {
    auto tile{Tile()}; auto layer{Layer()}; REQUIRE(tile.has_value()); REQUIRE(layer.has_value());
    auto tileBytes{recastnav::codec::EncodeTile(*tile)}; auto layerBytes{recastnav::codec::EncodeLayer(*layer)};
    REQUIRE(tileBytes.has_value()); REQUIRE(layerBytes.has_value());
    for (std::size_t length{};length<tileBytes->size();++length) { CHECK_FALSE(recastnav::codec::DecodeTile(std::span<const std::byte>{*tileBytes}.first(length)).has_value()); }
    for (std::size_t length{};length<layerBytes->size();++length) { CHECK_FALSE(recastnav::codec::DecodeLayer(std::span<const std::byte>{*layerBytes}.first(length)).has_value()); }
}
TEST_CASE("Codec framing distinguishes version, magic, corruption, trailing bytes and budgets") {
    auto tile{Tile()}; REQUIRE(tile.has_value()); auto encoded{recastnav::codec::EncodeTile(*tile)}; REQUIRE(encoded.has_value());
    auto bytes{*encoded}; bytes[4]=std::byte{2}; auto version{recastnav::codec::DecodeTile(bytes)};
    REQUIRE_FALSE(version.has_value()); CHECK(version.error().GetError()==Error::UnsupportedVersion);
    bytes=*encoded; bytes[0]=std::byte{0}; auto magic{recastnav::codec::DecodeTile(bytes)};
    REQUIRE_FALSE(magic.has_value()); CHECK(magic.error().GetError()==Error::InvalidMagic);
    bytes=*encoded; bytes.back()^=std::byte{1}; auto corrupt{recastnav::codec::DecodeTile(bytes)};
    REQUIRE_FALSE(corrupt.has_value()); CHECK(corrupt.error().GetError()==Error::ChecksumMismatch);
    bytes=*encoded; bytes.push_back(std::byte{}); auto trailing{recastnav::codec::DecodeTile(bytes)};
    REQUIRE_FALSE(trailing.has_value()); CHECK(trailing.error().GetError()==Error::TrailingData);
    auto limited{recastnav::codec::EncodeTile(*tile,DocumentLimits{32u})};
    REQUIRE_FALSE(limited.has_value()); CHECK(limited.error().GetError()==Error::BudgetExceeded);
    CHECK_FALSE(recastnav::codec::DecodeLayer(*encoded).has_value());
}
TEST_CASE("Hostile count is rejected before allocation even when packet integrity is recomputed") {
    auto tile{Tile()}; REQUIRE(tile.has_value()); auto encoded{recastnav::codec::EncodeTile(*tile)}; REQUIRE(encoded.has_value());
    Put64(*encoded,132u,std::numeric_limits<std::uint64_t>::max()); Rehash(*encoded);
    auto result{recastnav::codec::DecodeTile(*encoded)}; REQUIRE_FALSE(result.has_value()); CHECK(result.error().GetError()==Error::BudgetExceeded);
}
TEST_CASE("Checksummed geometry still undergoes semantic admission and empty tiles remain valid") {
    auto tile{Tile()}; REQUIRE(tile.has_value()); auto bytes{recastnav::codec::EncodeTile(*tile)}; REQUIRE(bytes.has_value());
    // First vertex X follows the 100-byte metadata and four eight-byte counts.
    const auto nanBits{std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN())};
    for (unsigned i{};i<4u;++i) { (*bytes)[164u+i]=static_cast<std::byte>(static_cast<std::uint8_t>(nanBits>>(8u*i))); }
    Rehash(*bytes); auto nonfinite{recastnav::codec::DecodeTile(*bytes)};
    REQUIRE_FALSE(nonfinite.has_value()); CHECK(nonfinite.error().GetError()==Error::NonFiniteGeometry);
    auto empty{TileDocument::Create(Metadata(),{}, {})}; REQUIRE(empty.has_value());
    auto emptyBytes{recastnav::codec::EncodeTile(*empty)}; REQUIRE(emptyBytes.has_value());
    auto decoded{recastnav::codec::DecodeTile(*emptyBytes)}; REQUIRE(decoded.has_value()); CHECK(decoded->GetPolygons().empty());
}
TEST_CASE("Invalid boolean and noncanonical optional encoding are refused after integrity verification") {
    auto tile{Tile()}; REQUIRE(tile.has_value()); auto bytes{recastnav::codec::EncodeTile(*tile)}; REQUIRE(bytes.has_value());
    (*bytes)[256u]=std::byte{2}; Rehash(*bytes); auto decoded{recastnav::codec::DecodeTile(*bytes)};
    REQUIRE_FALSE(decoded.has_value()); CHECK(decoded.error().GetError()==Error::InvalidEncoding);
    auto layer{LayerDocument::Create(Metadata(),1u,1u,{0u,0u,0u,0u},0u,0u,std::vector<LayerCell>{LayerCell{}})};
    REQUIRE(layer.has_value()); auto layerBytes{recastnav::codec::EncodeLayer(*layer)}; REQUIRE(layerBytes.has_value());
    (*layerBytes)[197u]=std::byte{1}; Rehash(*layerBytes); auto badOptional{recastnav::codec::DecodeLayer(*layerBytes)};
    REQUIRE_FALSE(badOptional.has_value()); CHECK(badOptional.error().GetError()==Error::InvalidEncoding);
}
TEST_CASE("Tile admission rejects invalid adjacency and reversed or nonconvex surface winding") {
    const std::vector<Vec3> vertices{{0,0,0},{1,0,0},{1,0,-1},{0,0,-1}};
    auto invalid{TileDocument::Create(Metadata(),vertices,std::vector<Polygon>{Polygon{
        {VertexIndex{0},VertexIndex{1},VertexIndex{2}}, {PolygonIndex{0},std::monostate{},std::monostate{}}}})};
    REQUIRE_FALSE(invalid.has_value()); CHECK(invalid.error().GetError()==Error::InvalidTopology);
    auto reversed{TileDocument::Create(Metadata(),vertices,std::vector<Polygon>{Polygon{
        {VertexIndex{0},VertexIndex{3},VertexIndex{2},VertexIndex{1}},std::vector<EdgeConnection>(4)}})};
    REQUIRE_FALSE(reversed.has_value()); CHECK(reversed.error().GetError()==Error::NonConvexPolygon);
    auto crossed{TileDocument::Create(Metadata(),vertices,std::vector<Polygon>{Polygon{
        {VertexIndex{0},VertexIndex{2},VertexIndex{1},VertexIndex{3}},std::vector<EdgeConnection>(4)}})};
    CHECK_FALSE(crossed.has_value());
}
TEST_CASE("Local polygon neighbours require an actual reversed shared edge and reciprocal identity") {
    const std::vector<Vec3> vertices{{0,0,0},{1,0,0},{1,0,-1},{0,0,-1}};
    std::vector<Polygon> polygons{};
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0},VertexIndex{1},VertexIndex{2}},
        std::vector<EdgeConnection>{std::monostate{},std::monostate{},PolygonIndex{1}});
    polygons.emplace_back(std::vector<VertexIndex>{VertexIndex{0},VertexIndex{2},VertexIndex{3}},
        std::vector<EdgeConnection>{PolygonIndex{0},std::monostate{},std::monostate{}});
    CHECK(TileDocument::Create(Metadata(),vertices,polygons).has_value());
    polygons[1]=Polygon{{VertexIndex{0},VertexIndex{2},VertexIndex{3}},std::vector<EdgeConnection>(3)};
    CHECK_FALSE(TileDocument::Create(Metadata(),vertices,std::move(polygons)).has_value());
}
TEST_CASE("Build geometry validates full triangles while preserving vertical wall input") {
    const std::array<Vec3,3> vertices{{{0,0,0},{1,0,0},{0,1,0}}};
    const std::array<Triangle,1> wall{{Triangle{{VertexIndex{0},VertexIndex{1},VertexIndex{2}}}}};
    CHECK(ValidateBuildInput(vertices,wall,BuildProfile{}).has_value());
    const std::array<Triangle,1> invalid{{Triangle{{VertexIndex{0},VertexIndex{1},VertexIndex{3}}}}};
    auto bounds{ValidateBuildInput(vertices,invalid,BuildProfile{})}; REQUIRE_FALSE(bounds.has_value()); CHECK(bounds.error().GetError()==Error::InvalidIndex);
    const std::array<Triangle,1> degenerate{{Triangle{{VertexIndex{0},VertexIndex{1},VertexIndex{1}}}}};
    CHECK_FALSE(ValidateBuildInput(vertices,degenerate,BuildProfile{}).has_value());
    CHECK_FALSE(ValidateBuildInput(vertices,wall,BuildProfile{0.0f}).has_value());
    auto nonfinite{vertices}; nonfinite[1]=Vec3{std::numeric_limits<float>::infinity(),0,0};
    CHECK_FALSE(ValidateBuildInput(nonfinite,wall,BuildProfile{}).has_value());
}
TEST_CASE("Layer admission rejects overflowing dimensions, absent support and one-way grid connectivity") {
    CHECK_FALSE(LayerDocument::Create(Metadata(),std::numeric_limits<std::size_t>::max(),2u,{0,0,0,0},0,1,{}).has_value());
    CHECK_FALSE(LayerDocument::Create(Metadata(),1u,1u,{0,0,0,0},0,1,std::vector<LayerCell>{LayerCell{{},63u}}).has_value());
    std::array<LayerConnection,4> first{}; first[2]=LayerConnection{true,false};
    CHECK_FALSE(LayerDocument::Create(Metadata(),2u,1u,{0,0,1,0},0,1,
        std::vector<LayerCell>{LayerCell{0u,63u,first},LayerCell{0u,63u}}).has_value());
    CHECK_FALSE(LayerDocument::Create(Metadata(),1u,1u,{0,0,0,0},400,800,std::vector<LayerCell>{LayerCell{401u,63u}}).has_value());
}
TEST_CASE("Detail admission rejects gaps, duplicate triangles, overlap and inconsistent boundary flags") {
    const DetailTriangle first{{DetailVertexIndex{0},DetailVertexIndex{1},DetailVertexIndex{2}}, {true,true,false}};
    const DetailTriangle second{{DetailVertexIndex{0},DetailVertexIndex{2},DetailVertexIndex{3}}, {false,true,true}};
    SUBCASE("missing half") { CHECK_FALSE(WithDetail({first}).has_value()); }
    SUBCASE("duplicate positive triangle") { CHECK_FALSE(WithDetail({first,first,second}).has_value()); }
    SUBCASE("overlapping diagonals and uncovered region") {
        CHECK_FALSE(WithDetail({first,DetailTriangle{{DetailVertexIndex{0},DetailVertexIndex{1},DetailVertexIndex{3}},
            {true,false,true}}}).has_value());
    }
    SUBCASE("interior edge incorrectly marked boundary") {
        CHECK_FALSE(WithDetail({DetailTriangle{{DetailVertexIndex{0},DetailVertexIndex{1},DetailVertexIndex{2}},
            {true,true,true}},second}).has_value());
    }
    SUBCASE("perimeter edge incorrectly marked interior") {
        CHECK_FALSE(WithDetail({DetailTriangle{{DetailVertexIndex{0},DetailVertexIndex{1},DetailVertexIndex{2}},
            {false,true,false}},second}).has_value());
    }
    SUBCASE("same projected vertex under another index") {
        CHECK_FALSE(WithDetail({first,second},{{0,1,0}}).has_value());
    }
}
TEST_CASE("Complete detail fans and split perimeter segments retain exact coverage") {
    std::vector<DetailTriangle> fan{};
    for (std::size_t i{};i<4u;++i) {
        fan.emplace_back(std::array{DetailVertexIndex{i},DetailVertexIndex{(i+1u)%4u},DetailVertexIndex{4u}},
            std::array{true,false,false});
    }
    CHECK(WithDetail(std::move(fan),{{0.5f,0,-0.5f}}).has_value());
    CHECK(WithDetail({
        DetailTriangle{{DetailVertexIndex{0},DetailVertexIndex{4},DetailVertexIndex{3}},{true,false,true}},
        DetailTriangle{{DetailVertexIndex{4},DetailVertexIndex{2},DetailVertexIndex{3}},{false,true,false}},
        DetailTriangle{{DetailVertexIndex{4},DetailVertexIndex{1},DetailVertexIndex{2}},{true,true,false}}
    },{{0.5f,0,0}}).has_value());
}
TEST_CASE("A checksummed SRNT cannot publish a truncated detail surface or wrong boundary flag") {
    auto source{Tile()}; REQUIRE(source.has_value()); auto encoded{recastnav::codec::EncodeTile(*source)}; REQUIRE(encoded.has_value());
    SUBCASE("missing second half") {
        // Fixture: one polygon with four vertices and two categories, then an empty
        // additional-vertex array and two 27-byte detail triangles.
        Put64(*encoded,317u,1u);
        encoded->erase(encoded->begin()+352,encoded->begin()+379);
        Put64(*encoded,8u,encoded->size()-32u); Rehash(*encoded);
        auto result{recastnav::codec::DecodeTile(*encoded)};
        REQUIRE_FALSE(result.has_value()); CHECK(result.error().GetError()==Error::InvalidDetailMesh);
    }
    SUBCASE("interior diagonal marked boundary") {
        (*encoded)[351u]=std::byte{1}; Rehash(*encoded);
        auto result{recastnav::codec::DecodeTile(*encoded)};
        REQUIRE_FALSE(result.has_value()); CHECK(result.error().GetError()==Error::InvalidDetailMesh);
    }
}
TEST_CASE("Layer neighbour and portal are mutually exclusive even in a checksummed packet") {
    auto source{Layer()}; REQUIRE(source.has_value());
    auto cells{std::vector<LayerCell>{source->GetCells().begin(),source->GetCells().end()}};
    auto connections{cells[0].GetConnections()}; connections[2]=LayerConnection{true,true};
    cells[0]=LayerCell{300u,63u,connections};
    auto invalid{LayerDocument::Create(source->GetMetadata(),2u,1u,{0,0,1,0},400u,800u,std::move(cells))};
    REQUIRE_FALSE(invalid.has_value()); CHECK(invalid.error().GetError()==Error::InvalidLayer);
    auto bytes{recastnav::codec::EncodeLayer(*source)}; REQUIRE(bytes.has_value());
    (*bytes)[210u]=std::byte{1}; Rehash(*bytes); auto decoded{recastnav::codec::DecodeLayer(*bytes)};
    REQUIRE_FALSE(decoded.has_value()); CHECK(decoded.error().GetError()==Error::InvalidLayer);
}
