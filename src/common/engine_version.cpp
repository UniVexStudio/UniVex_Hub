// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_version.h"

namespace UVE::Core {

std::string VersionUVE::ToStringUVE() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch) +
           "+build." + std::to_string(build);
}

} // namespace UVE::Core
