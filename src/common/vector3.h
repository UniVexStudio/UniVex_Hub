// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Math {

struct Vector3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    constexpr Vector3() noexcept = default;
    constexpr Vector3(const float xIn, const float yIn, const float zIn) noexcept : x(xIn), y(yIn), z(zIn) {}

    [[nodiscard]] constexpr bool operator==(const Vector3&) const noexcept = default;
};

} // namespace UVE::Math
