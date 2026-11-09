#include "tests/test_support.hpp"
#include "tests/testcases.hpp"
#include "search/query.hpp"
#include "index/binary_io.hpp"

#include <array>
#include <cstdint>
#include <fstream>

namespace search_tests {

void test_64bit_file_offsets() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "sparse.idx";
    constexpr std::uint64_t text_bytes = 0xfffffff0ULL;
    constexpr std::uint64_t doc_data = 112;
    constexpr std::uint64_t doc_size = 13 + text_bytes;
    constexpr std::uint64_t term_offsets = doc_data + doc_size;
    constexpr std::uint64_t term_data = term_offsets + 16;
    constexpr std::uint64_t postings = term_data + 21;
    constexpr std::uint64_t end = postings + 4;
    std::ofstream out(file, std::ios::binary);
    out.write("SIRIDX02", 8);
    search::detail::write_number(out, 2, 4);
    search::detail::write_number(out, 1, 4);
    for (const auto value : std::array<std::uint64_t, 10>{1, 1, 1, text_bytes, 96, doc_data,
                                                          term_offsets, term_data, postings, end}) {
        search::detail::write_number(out, value, 8);
    }
    search::detail::write_number(out, 0, 8);
    search::detail::write_number(out, doc_size, 8);
    search::detail::write_string(out, "x");
    search::detail::write_string(out, "");
    search::detail::write_number(out, text_bytes, 4);
    out.seekp(static_cast<std::streamoff>(term_offsets));
    search::detail::write_number(out, 0, 8);
    search::detail::write_number(out, 21, 8);
    search::detail::write_string(out, "alpha");
    search::detail::write_number(out, 0, 8);
    search::detail::write_number(out, 1, 4);
    search::detail::write_number(out, 0, 4);
    out.close();
    SEARCH_TEST_CHECK(static_cast<bool>(out));
    SEARCH_TEST_CHECK(fs::file_size(file) == end);
    const auto index = Index::load(file);
    SEARCH_TEST_CHECK(index.text_bytes() == text_bytes);
    SEARCH_TEST_CHECK(index.document_count() == 1);
    SEARCH_TEST_CHECK(index.term_count() == 1);
    SEARCH_TEST_CHECK(index.term(0).word == "alpha");
    SEARCH_TEST_CHECK(index.find("alpha")->count == 1);
    check_results(index, "alpha", {0});
    check_results(index, "unknown", {});
}

}
