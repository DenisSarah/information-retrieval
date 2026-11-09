#include "tests/test_support.hpp"
#include "tests/testcases.hpp"
#include "search/query.hpp"
#include "index/builder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace search_tests {

void test_external_builder() {
    TemporaryDirectory temporary;
    std::vector<Document> documents;
    for (unsigned i = 0; i < 73; ++i) {
        std::string text = i % 7 == 0 ? "" : "common common x" + std::to_string(i % 13);
        if (i % 3 == 0)
            text += " third Ёж";
        documents.push_back({"external-" + std::to_string(i), "Title", text});
    }
    documents[0].external_id = "common";
    documents[1].external_id = "third";
    documents[2].external_id = std::string("external\0id", 11);
    const auto build = [&](const fs::path& file, std::size_t budget, std::size_t fan_in) {
        search::BuildOptions options;
        options.memory_bytes = budget;
        options.merge_fan_in = fan_in;
        search::IndexBuilder builder(file, options);
        for (const auto& document : documents)
            builder.add(document);
        const auto stats = builder.finish();
        const auto built = Index::load(file);
        check_index_documents(built, documents);
        SEARCH_TEST_CHECK(stats.documents == built.document_count());
        SEARCH_TEST_CHECK(stats.terms == built.term_count());
        SEARCH_TEST_CHECK(stats.postings == built.posting_count());
        SEARCH_TEST_CHECK(stats.text_bytes == built.text_bytes());
        expect_error([&] { builder.add({"later", "", "late"}); });
        expect_error([&] { builder.finish(); });
    };
    const auto tiny = temporary.path() / "tiny.idx";
    const auto large = temporary.path() / "large.idx";
    const auto uneven = temporary.path() / "uneven.idx";
    build(tiny, 512, 2);
    build(large, 1024 * 1024, 32);
    build(uneven, 512, 3);
    SEARCH_TEST_CHECK(read_bytes(tiny) == read_bytes(uneven));
    SEARCH_TEST_CHECK(read_bytes(tiny) == read_bytes(large));
    const auto disk = Index::load(tiny);
    disk.verify();
    check_index_documents(disk, documents);
    check_index_invariants(disk);
    const std::vector<std::string> queries{"common",
                                           "NOT common",
                                           "common AND NOT third",
                                           "x1 OR еж",
                                           "(x1 OR x2) AND NOT x3",
                                           "NOT (common OR third)",
                                           "third"};
    std::vector<std::vector<DocId>> expected(queries.size());
    for (DocId id = 0; id < documents.size(); ++id) {
        const bool common = id % 7 != 0;
        const bool third = id % 3 == 0;
        const bool x1 = common && id % 13 == 1;
        const bool x2 = common && id % 13 == 2;
        const bool x3 = common && id % 13 == 3;
        const std::array<bool, 7> matches{common,      !common,           common && !third,
                                          x1 || third, (x1 || x2) && !x3, !(common || third),
                                          third};
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (matches[i])
                expected[i].push_back(id);
        }
    }
    for (std::size_t i = 0; i < queries.size(); ++i) {
        check_results(disk, queries[i], expected[i]);
    }
    for (std::uint32_t target = 0; target < documents.size() + 3; ++target) {
        auto cursor = search::make_query(disk, "common");
        const auto match = std::lower_bound(expected[0].begin(), expected[0].end(), target);
        SEARCH_TEST_CHECK(cursor->advance(target) ==
                          (match == expected[0].end() ? search::end_doc : *match));
        SEARCH_TEST_CHECK(collect(*cursor) == std::vector<DocId>(match, expected[0].end()));
    }
    const std::vector<Document> replacement{{"replacement", "", "changed"}};
    build_index(tiny, replacement);
    check_index_documents(disk, documents);
    check_index_documents(Index::load(tiny), replacement);
    build_index(tiny, documents);
    check_indexes_equal(disk, Index::load(tiny));
    check_results(disk, "NOT unknown", [&] {
        std::vector<DocId> ids;
        for (DocId i = 0; i < documents.size(); ++i)
            ids.push_back(i);
        return ids;
    }());

    const auto old_bytes = read_bytes(tiny);
    {
        search::BuildOptions options;
        options.memory_bytes = 1;
        options.merge_fan_in = 2;
        search::IndexBuilder builder(tiny, options);
        expect_error([&] {
            builder.add({"duplicate", "", ""});
            for (unsigned i = 0; i < 20; ++i)
                builder.add({"other" + std::to_string(i), "", "term"});
            builder.add({"duplicate", "", ""});
            builder.finish();
        });
    }
    SEARCH_TEST_CHECK(read_bytes(tiny) == old_bytes);
    {
        search::IndexBuilder builder(tiny);
        builder.add({"valid", "", "good"});
        expect_error([&] { builder.add({"broken", "", "\xc2"}); });
        expect_error([&] { builder.finish(); });
    }
    SEARCH_TEST_CHECK(read_bytes(tiny) == old_bytes);
    {
        search::IndexBuilder abandoned(tiny);
        abandoned.add({"not-published", "", "draft"});
    }
    SEARCH_TEST_CHECK(read_bytes(tiny) == old_bytes);
    for (const auto& entry : fs::directory_iterator(temporary.path())) {
        SEARCH_TEST_CHECK(!entry.is_directory());
    }
    search::BuildOptions invalid;
    invalid.memory_bytes = 0;
    expect_error([&] { search::IndexBuilder builder(large, invalid); });
    invalid.memory_bytes = 512;
    invalid.merge_fan_in = 1;
    expect_error([&] { search::IndexBuilder builder(large, invalid); });
}

}
