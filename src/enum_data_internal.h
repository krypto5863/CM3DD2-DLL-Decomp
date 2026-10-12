#pragma once
#include "cm3d2/enum_data_api.h"
#include <cstdint>
#include <map>
#include <string>

namespace cm3d2::internal {
// Semantic reconstruction, not a binary-compatible MSVC 2017 tree layout.
struct EnumData {
    std::map<std::string, std::int32_t> by_text;
    std::map<std::int32_t, std::string> by_number;
};
}
