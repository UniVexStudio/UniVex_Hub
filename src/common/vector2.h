// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Math {

struct Vector2UVE final {
    float x = 0.0F;
    float y = 0.0F;

    constexpr Vector2UVE() noexcept = default;
    constexpr Vector2UVE(const float xIn, const float yIn) noexcept : x(xIn), y(yIn) {}

    [[nodiscard]] constexpr bool operator==(const Vector2UVE&) const noexcept = default;
};

} // namespace UVE::Math
