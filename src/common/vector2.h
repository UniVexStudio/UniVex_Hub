// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Math {

struct Vector2 final {
    float x = 0.0F;
    float y = 0.0F;

    constexpr Vector2() noexcept = default;
    constexpr Vector2(const float xIn, const float yIn) noexcept : x(xIn), y(yIn) {}

    [[nodiscard]] constexpr bool operator==(const Vector2&) const noexcept = default;
};

} // namespace UVE::Math
