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
module recastnav.codec.layer;
import std;
#include "CodecDetail.hpp"

namespace recastnav::codec {
std::expected<std::vector<std::byte>, Failure> EncodeLayer(const LayerDocument& document, DocumentLimits limits) noexcept try {
    if (!limits.IsValid()) { return std::unexpected{Failure{Error::InvalidLimits}}; }
    if (document.GetCells().size() > limits.GetMaximumLayerCells()) { return std::unexpected{Failure{Error::BudgetExceeded}}; }
    Writer writer{limits.GetMaximumBytes(), kLayerMagic}; writer.Metadata(document.GetMetadata());
    writer.Count(document.GetWidth()); writer.Count(document.GetHeight());
    const auto& region{document.GetUsableRegion()};
    writer.Count(region.GetMinimumX()); writer.Count(region.GetMinimumZ()); writer.Count(region.GetMaximumX()); writer.Count(region.GetMaximumZ());
    writer.U32(document.GetMinimumHeight()); writer.U32(document.GetMaximumHeight()); writer.Count(document.GetCells().size());
    for (const auto& cell : document.GetCells()) {
        writer.Bool(cell.GetHeight().has_value()); writer.U32(cell.GetHeight().value_or(0u)); writer.U32(cell.GetArea());
        for (const auto& connection : cell.GetConnections()) { writer.Bool(connection.HasNeighbour()); writer.Bool(connection.IsPortal()); }
    }
    return writer.Finish();
} catch (const CodecFailure& failure) { return std::unexpected{failure.GetFailure()}; }
catch (const std::bad_alloc&) { return std::unexpected{Failure{Error::AllocationFailure}}; }
catch (const std::length_error&) { return std::unexpected{Failure{Error::SizeOverflow}}; }

std::expected<LayerDocument, Failure> DecodeLayer(std::span<const std::byte> bytes, DocumentLimits limits) noexcept try {
    auto reader{OpenFrame(bytes, limits, kLayerMagic)}; const auto metadata{reader.Metadata()};
    const auto width{reader.Index()}; const auto height{reader.Index()};
    const auto minimumX{reader.Index()}; const auto minimumZ{reader.Index()}; const auto maximumX{reader.Index()}; const auto maximumZ{reader.Index()};
    const auto minimumHeight{reader.U32()}; const auto maximumHeight{reader.U32()};
    const auto count{reader.Count(limits.GetMaximumLayerCells(), 17u)};
    const auto product{CheckedProduct(width, height, limits.GetMaximumLayerCells())};
    if (!product || *product != count || width == 0u || height == 0u) { Refuse(Error::InvalidLayer, reader.Position()); }
    std::vector<LayerCell> cells{}; cells.reserve(count);
    for (std::size_t i{}; i < count; ++i) {
        const auto hasHeight{reader.Bool()}; const auto cellHeight{reader.U32()}; const auto area{reader.U32()};
        if (!hasHeight && cellHeight != 0u) { Refuse(Error::InvalidEncoding, reader.Position() - 8u); }
        std::array<LayerConnection, 4> connections{};
        for (auto& connection : connections) { const auto neighbour{reader.Bool()}; const auto portal{reader.Bool()}; connection = LayerConnection{neighbour, portal}; }
        cells.emplace_back(hasHeight ? std::optional{cellHeight} : std::nullopt, area, connections);
    }
    reader.End();
    return LayerDocument::Create(metadata, width, height, LayerRegion{minimumX, minimumZ, maximumX, maximumZ},
        minimumHeight, maximumHeight, std::move(cells), limits);
} catch (const CodecFailure& failure) { return std::unexpected{failure.GetFailure()}; }
catch (const std::bad_alloc&) { return std::unexpected{Failure{Error::AllocationFailure}}; }
catch (const std::length_error&) { return std::unexpected{Failure{Error::SizeOverflow}}; }
}
