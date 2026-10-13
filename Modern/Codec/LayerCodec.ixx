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
export module recastnav.codec.layer;
import std;
export import recastnav.document;
export namespace recastnav::codec {
// Version 1: little-endian field-wise SRNL, explicit cells/connections; no runtime layout aliasing.
[[nodiscard]] std::expected<std::vector<std::byte>, Failure> EncodeLayer(const LayerDocument& document, DocumentLimits limits = {}) noexcept;
[[nodiscard]] std::expected<LayerDocument, Failure> DecodeLayer(std::span<const std::byte> bytes, DocumentLimits limits = {}) noexcept;
}
