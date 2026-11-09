#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include <string>
#include <vector>

namespace search_tests {

void test_empty_documents_and_invalid_input() {
    const auto empty = make_index({});
    SEARCH_TEST_CHECK(empty.document_count() == 0);
    SEARCH_TEST_CHECK(empty.term_count() == 0);
    SEARCH_TEST_CHECK(empty.posting_count() == 0);
    check_results(empty, "anything", {});
    check_results(empty, "NOT anything", {});

    const auto no_words = make_index({{"first.txt", "", ""}, {"second.md", "", "\n---🙂"}});
    SEARCH_TEST_CHECK(no_words.document_count() == 2);
    SEARCH_TEST_CHECK(no_words.term_count() == 0);
    SEARCH_TEST_CHECK(no_words.posting_count() == 0);
    check_results(no_words, "anything", {});
    check_results(no_words, "NOT anything", {0, 1});

    expect_error([] { make_index({{"", "", "text"}}); });
    expect_error([] { make_index({{"same", "", "a"}, {"same", "", "b"}}); });
    expect_error([] { make_index({{"x", "", "\xc2"}}); });
    expect_error([] { make_index({{"x", "\xc2", "valid"}}); });
    expect_error([] { make_index({{"\xc2", "", "valid"}}); });

    empty.verify();
    no_words.verify();
}

}
