#pragma once

#include "search/cursor.hpp"
#include "search/index.hpp"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace search_tests {

using search::DocId;
using search::Document;
using search::Index;
namespace fs = std::filesystem;

[[noreturn]] void fail(const char* expression, int line, const std::string& detail = {});

template <typename Function> void expect_error(Function&& function) {
    bool threw = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    if (!threw)
        fail("threw", __LINE__);
}

class TemporaryDirectory {
public:
    TemporaryDirectory();
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_bytes(const fs::path& path, const std::string& bytes);
std::string read_bytes(const fs::path& path);
std::vector<DocId> collect(search::Cursor& cursor,
                           std::size_t limit = std::numeric_limits<std::size_t>::max());
void check_results(const Index& index, const std::string& query,
                   const std::vector<DocId>& expected);
Index build_index(const fs::path& file, const std::vector<Document>& documents);
Index make_index(const std::vector<Document>& documents);
void check_index_documents(const Index& index, const std::vector<Document>& documents);
void check_indexes_equal(const Index& left, const Index& right);
void check_index_invariants(const Index& index);
Index boolean_fixture();

}

#define SEARCH_TEST_CHECK(expression)                                                              \
    do {                                                                                           \
        if (!(expression))                                                                         \
            ::search_tests::fail(#expression, __LINE__);                                           \
    } while (false)
