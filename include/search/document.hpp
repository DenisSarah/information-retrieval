#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace search {

using DocId = std::uint32_t;
inline constexpr DocId end_doc = std::numeric_limits<DocId>::max();

struct Document {
    std::string external_id;
    std::string title;
    std::string text;
};

}
