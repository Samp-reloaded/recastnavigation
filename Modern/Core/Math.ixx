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
export module recastnav.math;
import std;

export namespace recastnav {
// Recast coordinates: Y is up, XZ is the ground plane; distances are caller-declared world units.
class Vec3 final {
public:
    constexpr Vec3(float x = 0, float y = 0, float z = 0) noexcept : m_x{x}, m_y{y}, m_z{z} {}
    [[nodiscard]] constexpr float GetX() const noexcept { return m_x; }
    [[nodiscard]] constexpr float GetY() const noexcept { return m_y; }
    [[nodiscard]] constexpr float GetZ() const noexcept { return m_z; }
    [[nodiscard]] bool IsFinite() const noexcept { return std::isfinite(m_x) && std::isfinite(m_y) && std::isfinite(m_z); }
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
    [[nodiscard]] friend constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
        return {a.m_x + b.m_x, a.m_y + b.m_y, a.m_z + b.m_z};
    }
    [[nodiscard]] friend constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept {
        return {a.m_x - b.m_x, a.m_y - b.m_y, a.m_z - b.m_z};
    }
    [[nodiscard]] friend constexpr Vec3 operator*(const Vec3& a, float scale) noexcept {
        return {a.m_x * scale, a.m_y * scale, a.m_z * scale};
    }
private:
    float m_x{}, m_y{}, m_z{};
};
class Aabb final {
public:
    constexpr Aabb(Vec3 minimum = {}, Vec3 maximum = {}) noexcept : m_minimum{minimum}, m_maximum{maximum} {}
    [[nodiscard]] constexpr const Vec3& GetMinimum() const noexcept { return m_minimum; }
    [[nodiscard]] constexpr const Vec3& GetMaximum() const noexcept { return m_maximum; }
    [[nodiscard]] bool IsValid() const noexcept {
        return m_minimum.IsFinite() && m_maximum.IsFinite()
            && m_minimum.GetX() <= m_maximum.GetX() && m_minimum.GetY() <= m_maximum.GetY()
            && m_minimum.GetZ() <= m_maximum.GetZ();
    }
    [[nodiscard]] bool Contains(const Vec3& value) const noexcept {
        return value.IsFinite() && value.GetX() >= m_minimum.GetX() && value.GetX() <= m_maximum.GetX()
            && value.GetY() >= m_minimum.GetY() && value.GetY() <= m_maximum.GetY()
            && value.GetZ() >= m_minimum.GetZ() && value.GetZ() <= m_maximum.GetZ();
    }
    [[nodiscard]] constexpr bool operator==(const Aabb&) const noexcept = default;
private:
    Vec3 m_minimum{}, m_maximum{};
};
[[nodiscard]] inline double UpwardTurn(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    return (static_cast<double>(b.GetZ()) - a.GetZ()) * (static_cast<double>(c.GetX()) - b.GetX())
        - (static_cast<double>(b.GetX()) - a.GetX()) * (static_cast<double>(c.GetZ()) - b.GetZ());
}
}
