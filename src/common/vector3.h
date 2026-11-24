// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Math {

struct Vector3UVE final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    constexpr Vector3UVE() noexcept = default;
    constexpr Vector3UVE(const float xIn, const float yIn, const float zIn) noexcept : x(xIn), y(yIn), z(zIn) {}

    [[nodiscard]] constexpr bool operator==(const Vector3UVE&) const noexcept = default;
};

} // namespace UVE::Math
