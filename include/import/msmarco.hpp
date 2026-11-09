#pragma once

#include "search/document.hpp"

#include <filesystem>
#include <functional>

namespace search::detail {

using DocumentConsumer = std::function<void(Document)>;

void read_msmarco(const std::filesystem::path& input, const DocumentConsumer& consumer);

}
