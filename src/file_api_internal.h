#pragma once

#include "cm3d2/file_api.h"

#include <cstddef>

namespace cm3d2::internal {

const std::byte* fileData(CM3D2_FileHandle file);

} // namespace cm3d2::internal
