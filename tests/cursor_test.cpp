#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include "search/query.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace search_tests {

void test_cursor_contract() {
    const auto index = boolean_fixture();
    auto cursor = search::make_query(index, "a");
    SEARCH_TEST_CHECK(cursor->doc() == 0);
    SEARCH_TEST_CHECK(cursor->advance(0) == 0);
    SEARCH_TEST_CHECK(cursor->advance(1) == 1);
    SEARCH_TEST_CHECK(cursor->advance(0) == 1);
    SEARCH_TEST_CHECK(cursor->advance(1) == 1);
    SEARCH_TEST_CHECK(cursor->advance(2) == 3);
    SEARCH_TEST_CHECK(cursor->next() == 4);
    SEARCH_TEST_CHECK(cursor->advance(0) == 4);
    SEARCH_TEST_CHECK(cursor->next() == search::end_doc);
    SEARCH_TEST_CHECK(cursor->advance(0) == search::end_doc);
    SEARCH_TEST_CHECK(cursor->next() == search::end_doc);

    for (const auto& query :
         {"a", "a b", "a OR b", "a NOT b", "NOT a", "a AND unknown", "(a OR c) AND NOT b"}) {
        auto expected_cursor = search::make_query(index, query);
        const auto expected = collect(*expected_cursor);
        for (std::size_t limit = 0; limit <= expected.size() + 2; ++limit) {
            auto limited = search::make_query(index, query);
            const std::vector<DocId> prefix(expected.begin(),
                                            expected.begin() + std::min(limit, expected.size()));
            SEARCH_TEST_CHECK(collect(*limited, limit) == prefix);
        }
        auto exhausted = search::make_query(index, query);
        SEARCH_TEST_CHECK(exhausted->advance(search::end_doc) == search::end_doc);
        SEARCH_TEST_CHECK(exhausted->advance(0) == search::end_doc);
        SEARCH_TEST_CHECK(exhausted->next() == search::end_doc);
    }
}

void test_posting_advance_windows() {
    constexpr DocId document_count = 4097;
    const std::set<DocId> rare_ids{0,    1,    2,    63,   64,   65,   255,  256, 257,
                                   1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096};
    std::vector<Document> documents;
    std::vector<DocId> all;
    std::vector<DocId> every_three;
    std::vector<DocId> rare;
    for (DocId id = 0; id < document_count; ++id) {
        std::string text = "all";
        all.push_back(id);
        if (id % 3 == 0) {
            text += " every3";
            every_three.push_back(id);
        }
        if (rare_ids.count(id)) {
            text += " rare";
            rare.push_back(id);
        }
        documents.push_back({std::to_string(id) + ".txt", "", text});
    }
    const auto index = make_index(documents);
    const std::vector<std::pair<std::string, std::vector<DocId>>> cases{
        {"all", all}, {"every3", every_three}, {"rare", rare}};

    for (const auto& test : cases) {
        const auto& expected = test.second;
        std::vector<std::size_t> starts{
            0, 1, 2, 3, 7, 15, 16, 31, 63, 64, 127, 255, expected.size() / 2, expected.size() - 1};
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (const auto start : starts) {
            if (start >= expected.size())
                continue;
            std::vector<DocId> targets{0, expected[start], 4095, 4096, 4097, 8192, search::end_doc};
            for (DocId power = 1; power <= 4096; power *= 2) {
                targets.push_back(power - 1);
                targets.push_back(power);
                targets.push_back(power + 1);
                if (power < expected.size() - start) {
                    const auto id = expected[start + power];
                    targets.push_back(id - 1);
                    targets.push_back(id);
                    targets.push_back(id + 1);
                }
            }
            std::sort(targets.begin(), targets.end());
            targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
            for (const auto target : targets) {
                auto cursor = search::make_query(index, test.first);
                SEARCH_TEST_CHECK(cursor->advance(expected[start]) == expected[start]);
                auto match = std::lower_bound(expected.begin() + start, expected.end(), target);
                const auto wanted = match == expected.end() ? search::end_doc : *match;
                const auto actual = cursor->advance(target);
                if (actual != wanted) {
                    fail("posting advance equals lower_bound", __LINE__,
                         test.first + "; start=" + std::to_string(expected[start]) + "; target=" +
                             std::to_string(target) + "; expected=" + std::to_string(wanted) +
                             "; actual=" + std::to_string(actual));
                }
                SEARCH_TEST_CHECK(cursor->doc() == wanted);
                SEARCH_TEST_CHECK(cursor->advance(0) == wanted);
                SEARCH_TEST_CHECK(cursor->advance(wanted) == wanted);
                if (match != expected.end())
                    ++match;
                SEARCH_TEST_CHECK(cursor->next() ==
                                  (match == expected.end() ? search::end_doc : *match));
                SEARCH_TEST_CHECK(cursor->advance(search::end_doc) == search::end_doc);
                SEARCH_TEST_CHECK(cursor->advance(0) == search::end_doc);
                SEARCH_TEST_CHECK(cursor->next() == search::end_doc);
            }
        }
    }
}

}
