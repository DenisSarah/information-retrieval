#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace search {
class Index;
}

namespace cli {

std::string safe_inline(std::string_view value);
std::string snippet(std::string_view text);
void help(std::ostream& output);
void print_results(const search::Index& index, std::string_view query, std::size_t limit);
void show_document(const search::Index& index, std::uint64_t id);

}
