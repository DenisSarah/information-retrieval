#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include "search/query.hpp"

#include <string>
#include <utility>
#include <vector>

namespace search_tests {

void test_index_and_boolean_queries() {
    const auto index = boolean_fixture();
    check_index_invariants(index);
    SEARCH_TEST_CHECK(index.document(0).external_id == "z.md");
    SEARCH_TEST_CHECK(index.document(1).external_id == "a.md");
    SEARCH_TEST_CHECK(index.find("a")->count == 4);

    const std::vector<std::pair<std::string, std::vector<DocId>>> cases{
        {"a", {0, 1, 3, 4}},
        {"a a", {0, 1, 3, 4}},
        {"a OR a", {0, 1, 3, 4}},
        {"a AND b", {0, 3}},
        {"a b", {0, 3}},
        {"a,b", {0, 3}},
        {"a-b", {0, 3}},
        {"a/b", {0, 3}},
        {"a OR b", {0, 1, 2, 3, 4}},
        {"a OR b OR a OR b", {0, 1, 2, 3, 4}},
        {"a OR b AND c", {0, 1, 2, 3, 4}},
        {"(a OR b) AND c", {1, 2, 3}},
        {"a(b OR c)", {0, 1, 3}},
        {"(a OR b)c", {1, 2, 3}},
        {"NOT a", {2, 5, 6}},
        {"NOT NOT a", {0, 1, 3, 4}},
        {"a AND NOT b", {1, 4}},
        {"a NOT b", {1, 4}},
        {"NOT a b", {2}},
        {"NOT (a OR b)", {5, 6}},
        {"NOT a OR b", {0, 2, 3, 5, 6}},
        {"NOT (NOT a AND NOT b)", {0, 1, 2, 3, 4}},
        {"(a AND (b OR (c AND NOT b)))", {0, 1, 3}},
        {"unknown", {}},
        {"a AND unknown", {}},
        {"a OR unknown", {0, 1, 3, 4}},
        {"NOT unknown", {0, 1, 2, 3, 4, 5, 6}},
        {"and", {6}},
        {"or", {6}},
        {"not", {6}},
        {"AnD", {6}},
        {"ORfoo", {}},
    };
    for (const auto& test : cases)
        check_results(index, test.first, test.second);

    const auto russian = make_index({
        {"1.md", "", "Ёж ёж ЗЕЛЁНЫЙ"},
        {"2.md", "", "еж синий"},
    });
    check_results(russian, "ЕЖ", {0, 1});
    check_results(russian, "Ёж-зеленый", {0});
}

void test_invalid_queries_and_limits() {
    const auto index = boolean_fixture();
    const std::vector<std::string> invalid{
        "",      " \n\t ", "---",        "AND",       "OR",          "NOT",   "AND a",   "OR a",
        "a AND", "a OR",   "a AND OR b", "a OR OR b", "a AND AND b", "()",    "( )",     "(a",
        "a)",    "a ) b",  "(a))",       "a AND ()",  "a OR NOT",    "\"a\"", "a \"b\"", "'a'",
        "«a»",   "“a”",    "a\xc2",      "a && b",    "a AND !!! b"};
    for (const auto& query : invalid) {
        expect_error([&] { search::make_query(index, query); });
    }

    check_results(index, std::string(8192, 'x'), {});
    expect_error([&] { search::make_query(index, std::string(8193, 'x')); });

    std::string token_limit = "a";
    for (unsigned i = 1; i < 256; ++i)
        token_limit += " a";
    check_results(index, token_limit, {0, 1, 3, 4});
    expect_error([&] { search::make_query(index, token_limit + " a"); });

    const auto nested = [](unsigned depth) {
        return std::string(depth, '(') + "a" + std::string(depth, ')');
    };
    check_results(index, nested(64), {0, 1, 3, 4});
    expect_error([&] { search::make_query(index, nested(65)); });
    std::string unary_limit;
    for (unsigned i = 0; i < 64; ++i)
        unary_limit += "NOT ";
    check_results(index, unary_limit + "a", {0, 1, 3, 4});
    expect_error([&] { search::make_query(index, "NOT " + unary_limit + "a"); });
}

}
