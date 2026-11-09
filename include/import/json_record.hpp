#pragma once

#include "search/document.hpp"

#include <string_view>

namespace search::detail {

Document parse_msmarco_record(std::string_view input, std::string* url = nullptr);

}
