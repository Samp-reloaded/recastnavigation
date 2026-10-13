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
// Included only inside codec implementation units after importing std and recastnav.document.
namespace {
using recastnav::Error;
using recastnav::Failure;
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
static_assert(sizeof(float) == 4u && std::numeric_limits<float>::is_iec559
    && std::numeric_limits<float>::digits == 24);
class CodecFailure final {
public:
    explicit CodecFailure(Failure failure) noexcept : m_failure{failure} {}
    [[nodiscard]] const Failure& GetFailure() const noexcept { return m_failure; }
private:
    Failure m_failure;
};
[[noreturn]] void Refuse(Error error, std::size_t offset = 0) { throw CodecFailure{Failure{error, offset}}; }
constexpr std::size_t kFrameBytes{32u};
constexpr std::uint32_t kTileMagic{0x544e5253u};
constexpr std::uint32_t kLayerMagic{0x4c4e5253u};
[[nodiscard]] std::uint64_t PayloadHash(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash{14695981039346656037ull};
    for (const auto value : bytes) { hash ^= std::to_integer<std::uint8_t>(value); hash *= 1099511628211ull; }
    return hash;
}
class Writer final {
public:
    explicit Writer(std::size_t maximumBytes, std::uint32_t magic) : m_limit{maximumBytes} {
        U32(magic); U16(1u); U16(0u); U64(0u); U64(0u); U32(1u); U32(0u);
    }
    void U8(std::uint8_t value) {
        if (m_bytes.size() == m_limit) { Refuse(Error::BudgetExceeded, m_bytes.size()); }
        if (m_bytes.size() == m_bytes.capacity()) {
            const auto remaining{m_limit - m_bytes.capacity()};
            const auto growth{std::min(remaining, std::max(std::size_t{64u}, m_bytes.capacity()))};
            m_bytes.reserve(m_bytes.capacity() + growth);
        }
        m_bytes.push_back(static_cast<std::byte>(value));
    }
    void U16(std::uint16_t value) { for (unsigned i{}; i < 2u; ++i) { U8(static_cast<std::uint8_t>(value >> (i * 8u))); } }
    void U32(std::uint32_t value) { for (unsigned i{}; i < 4u; ++i) { U8(static_cast<std::uint8_t>(value >> (i * 8u))); } }
    void U64(std::uint64_t value) { for (unsigned i{}; i < 8u; ++i) { U8(static_cast<std::uint8_t>(value >> (i * 8u))); } }
    void Count(std::size_t value) { U64(static_cast<std::uint64_t>(value)); }
    void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
    void Bool(bool value) { U8(value ? 1u : 0u); }
    void Point(const recastnav::Vec3& point) { F32(point.GetX()); F32(point.GetY()); F32(point.GetZ()); }
    void Metadata(const recastnav::TileMetadata& metadata) {
        U64(std::bit_cast<std::uint64_t>(metadata.GetKey().GetX()));
        U64(std::bit_cast<std::uint64_t>(metadata.GetKey().GetZ())); U32(metadata.GetKey().GetLayer());
        for (const auto value : metadata.GetFingerprint()) { U8(std::to_integer<std::uint8_t>(value)); }
        Point(metadata.GetBounds().GetMinimum()); Point(metadata.GetBounds().GetMaximum());
        const auto& profile{metadata.GetProfile()};
        F32(profile.GetCellSize()); F32(profile.GetCellHeight()); F32(profile.GetAgentHeight());
        F32(profile.GetAgentRadius()); F32(profile.GetMaximumClimb()); F32(profile.GetMaximumSlopeDegrees());
    }
    void Categories(std::span<const std::uint32_t> categories, const recastnav::DocumentLimits& limits) {
        if (categories.size() > limits.GetMaximumCategories()) { Refuse(Error::BudgetExceeded); }
        Count(categories.size()); for (const auto value : categories) { U32(value); }
    }
    [[nodiscard]] std::vector<std::byte> Finish() noexcept {
        Patch64(8u, static_cast<std::uint64_t>(m_bytes.size() - kFrameBytes));
        Patch64(16u, PayloadHash(std::span<const std::byte>{m_bytes}.subspan(kFrameBytes)));
        return std::move(m_bytes);
    }
private:
    void Patch64(std::size_t offset, std::uint64_t value) noexcept {
        for (unsigned i{}; i < 8u; ++i) { m_bytes[offset + i] = static_cast<std::byte>(static_cast<std::uint8_t>(value >> (i * 8u))); }
    }
    std::size_t m_limit{};
    std::vector<std::byte> m_bytes{};
};
class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes, std::size_t base = 0) noexcept : m_bytes{bytes}, m_base{base} {}
    [[nodiscard]] std::size_t Position() const noexcept { return m_base + m_position; }
    [[nodiscard]] std::size_t Remaining() const noexcept { return m_bytes.size() - m_position; }
    [[nodiscard]] std::uint8_t U8() {
        if (Remaining() == 0u) { Refuse(Error::TruncatedInput, Position()); }
        return std::to_integer<std::uint8_t>(m_bytes[m_position++]);
    }
    [[nodiscard]] std::uint16_t U16() { std::uint16_t value{}; for (unsigned i{}; i < 2u; ++i) { value |= static_cast<std::uint16_t>(static_cast<unsigned>(U8()) << (8u * i)); } return value; }
    [[nodiscard]] std::uint32_t U32() { std::uint32_t value{}; for (unsigned i{}; i < 4u; ++i) { value |= static_cast<std::uint32_t>(U8()) << (8u * i); } return value; }
    [[nodiscard]] std::uint64_t U64() { std::uint64_t value{}; for (unsigned i{}; i < 8u; ++i) { value |= static_cast<std::uint64_t>(U8()) << (8u * i); } return value; }
    [[nodiscard]] std::size_t Index() {
        const auto value{U64()};
        if (value > std::numeric_limits<std::size_t>::max()) { Refuse(Error::SizeOverflow, Position() - 8u); }
        return static_cast<std::size_t>(value);
    }
    [[nodiscard]] std::size_t Count(std::size_t maximum, std::size_t minimumRecordBytes) {
        const auto value{Index()};
        if (value > maximum) { Refuse(Error::BudgetExceeded, Position() - 8u); }
        if (minimumRecordBytes != 0u && value > Remaining() / minimumRecordBytes) { Refuse(Error::TruncatedInput, Position()); }
        return value;
    }
    [[nodiscard]] float F32() { return std::bit_cast<float>(U32()); }
    [[nodiscard]] bool Bool() {
        const auto value{U8()}; if (value > 1u) { Refuse(Error::InvalidEncoding, Position() - 1u); } return value == 1u;
    }
    [[nodiscard]] recastnav::Vec3 Point() { const auto x{F32()}; const auto y{F32()}; const auto z{F32()}; return {x, y, z}; }
    [[nodiscard]] recastnav::TileMetadata Metadata() {
        const auto x{std::bit_cast<std::int64_t>(U64())}; const auto z{std::bit_cast<std::int64_t>(U64())}; const auto layer{U32()};
        recastnav::CookFingerprint fingerprint{}; for (auto& value : fingerprint) { value = static_cast<std::byte>(U8()); }
        const auto minimum{Point()}; const auto maximum{Point()};
        const auto cs{F32()}; const auto ch{F32()}; const auto height{F32()}; const auto radius{F32()}; const auto climb{F32()}; const auto slope{F32()};
        return recastnav::TileMetadata{{x, z, layer}, {minimum, maximum}, {cs, ch, height, radius, climb, slope}, fingerprint};
    }
    [[nodiscard]] std::vector<std::uint32_t> Categories(const recastnav::DocumentLimits& limits) {
        const auto count{Count(limits.GetMaximumCategories(), 4u)}; std::vector<std::uint32_t> values{}; values.reserve(count);
        for (std::size_t i{}; i < count; ++i) { values.push_back(U32()); } return values;
    }
    void End() const { if (Remaining() != 0u) { Refuse(Error::TrailingData, Position()); } }
private:
    std::span<const std::byte> m_bytes;
    std::size_t m_base{}, m_position{};
};
[[nodiscard]] Reader OpenFrame(std::span<const std::byte> bytes, const recastnav::DocumentLimits& limits, std::uint32_t magic) {
    if (!limits.IsValid()) { Refuse(Error::InvalidLimits); }
    if (bytes.size() > limits.GetMaximumBytes()) { Refuse(Error::BudgetExceeded); }
    if (bytes.size() < kFrameBytes) { Refuse(Error::TruncatedInput); }
    Reader reader{bytes};
    if (reader.U32() != magic) { Refuse(Error::InvalidMagic); }
    if (reader.U16() != 1u) { Refuse(Error::UnsupportedVersion, 4u); }
    if (reader.U16() != 0u) { Refuse(Error::InvalidEncoding, 6u); }
    const auto length{reader.U64()}; const auto checksum{reader.U64()};
    if (reader.U32() != 1u || reader.U32() != 0u) { Refuse(Error::InvalidEncoding, 24u); }
    if (length > bytes.size() - kFrameBytes) { Refuse(Error::TruncatedInput, kFrameBytes); }
    if (length < bytes.size() - kFrameBytes) { Refuse(Error::TrailingData, kFrameBytes + static_cast<std::size_t>(length)); }
    const auto payload{bytes.subspan(kFrameBytes)};
    if (PayloadHash(payload) != checksum) { Refuse(Error::ChecksumMismatch, 16u); }
    return Reader{payload, kFrameBytes};
}
}
