#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace web {

std::string source_url(const std::filesystem::path& corpus, std::string_view external_id);

}
