#pragma once

#include "search/cursor.hpp"

#include <memory>
#include <string_view>

namespace search {

class Index;

std::unique_ptr<Cursor> make_query(const Index& index, std::string_view query);

}
