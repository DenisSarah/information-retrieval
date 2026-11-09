#include "tests/test_support.hpp"
#include "index/builder.hpp"
#include "index/builder_runs.hpp"

#include <string>
#include <vector>

namespace search_tests {

void test_builder_buffer_boundaries() {
    TemporaryDirectory temporary;
    std::vector<Document> documents;
    for (unsigned i = 0; i < 2053; ++i) {
        documents.push_back({"record-" + std::to_string(i), "", "common tail"});
    }
    documents.back().text += " boundary";
    for (unsigned i = 0; i < 3; ++i) {
        documents.push_back({std::string(5000, 'x') + std::to_string(i), "", ""});
    }
    const auto build = [&](const fs::path& file, std::size_t budget, std::size_t fan_in) {
        search::BuildOptions options;
        options.memory_bytes = budget;
        options.merge_fan_in = fan_in;
        search::IndexBuilder builder(file, options);
        for (const auto& document : documents)
            builder.add(document);
        const auto stats = builder.finish();
        SEARCH_TEST_CHECK(stats.documents == documents.size());
        SEARCH_TEST_CHECK(stats.terms == 3);
        SEARCH_TEST_CHECK(stats.postings == 2053 * 2 + 1);
    };
    const auto single = temporary.path() / "single.idx";
    const auto merged = temporary.path() / "merged.idx";
    build(single, 16 * 1024 * 1024, 32);
    build(merged, 16 * 1024, 2);
    SEARCH_TEST_CHECK(read_bytes(single) == read_bytes(merged));
    const auto disk = Index::load(merged);
    disk.verify();
    check_index_documents(disk, documents);
    std::vector<DocId> expected;
    for (DocId i = 0; i < 2053; ++i)
        expected.push_back(i);
    check_results(disk, "common AND tail", expected);
    check_results(disk, "boundary", {2052});
}

void test_builder_run_blocks() {
    TemporaryDirectory temporary;
    const auto number = [](std::uint64_t value, unsigned width) {
        std::string bytes;
        for (unsigned i = 0; i < width; ++i) {
            bytes += static_cast<char>((value >> (8 * i)) & 255);
        }
        return bytes;
    };
    const auto postings = [&](const std::vector<DocId>& ids) {
        std::string bytes;
        for (const auto id : ids)
            bytes += number(id, 4);
        return bytes;
    };
    const auto record = [&](const std::string& word, const std::vector<DocId>& ids) {
        return number(word.size(), 4) + word + number(ids.size(), 4) + postings(ids);
    };
    const auto run = [&](const std::string& name, const std::string& bytes) {
        const auto path = temporary.path() / name;
        write_bytes(path, bytes);
        return search::detail::BuilderRun{path, 4};
    };
    const auto merge = [&](const std::vector<search::detail::BuilderRun>& runs) {
        return search::detail::merge_builder_runs(temporary.path() / "merged", runs);
    };
    const auto materialize = [&](const search::detail::BuilderRun& input,
                                 std::uint64_t documents = search::end_doc) {
        return search::detail::materialize_builder_terms(
            &input, documents, temporary.path() / "offsets", temporary.path() / "records",
            temporary.path() / "postings");
    };
    for (const unsigned size : {0, 1, 1023, 1024, 1025, 2048, 2053}) {
        std::vector<DocId> first;
        for (DocId i = 0; i < size; ++i)
            first.push_back(i);
        const std::vector<DocId> last =
            size ? std::vector<DocId>{65535, 65536, 0x01020304, search::end_doc - 1}
                 : std::vector<DocId>{};
        const auto a = run("a", (size ? record("same", first) : "") + record("tail", {7}) +
                                    record("a", {}) + record("same", {}));
        const auto b = run("b", (size ? record("same", last) : "") + record("b", {}));
        first.insert(first.end(), last.begin(), last.end());
        const auto merged = merge({a, b});
        SEARCH_TEST_CHECK(read_bytes(merged.path) == (size ? record("same", first) : "") +
                                                         record("tail", {7}) + record("a", {}) +
                                                         record("b", {}) + record("same", {}));
        const auto stats = materialize(merged);
        SEARCH_TEST_CHECK(stats.terms == (size ? 2 : 1));
        SEARCH_TEST_CHECK(stats.postings == first.size() + 1);
        SEARCH_TEST_CHECK(stats.bytes == (size ? 40 : 20));
        SEARCH_TEST_CHECK(read_bytes(temporary.path() / "offsets") ==
                          number(0, 8) + number(20, 8) + (size ? number(40, 8) : ""));
        SEARCH_TEST_CHECK(
            read_bytes(temporary.path() / "records") ==
            (size ? number(4, 4) + "same" + number(0, 8) + number(first.size(), 4) : "") +
                number(4, 4) + "tail" + number(first.size(), 8) + number(1, 4));
        SEARCH_TEST_CHECK(read_bytes(temporary.path() / "postings") ==
                          postings(first) + postings({7}));
    }
    const auto empty = run("empty", "");
    SEARCH_TEST_CHECK(read_bytes(merge({empty, empty}).path).empty());
    SEARCH_TEST_CHECK(materialize(empty).postings == 0);
    const auto rejected = [&](const std::string& bytes) {
        const auto bad = run("bad", bytes);
        expect_error([&] { merge({bad}); });
        expect_error([&] { materialize(bad); });
    };
    std::vector<DocId> ids;
    for (DocId i = 0; i < 2053; ++i)
        ids.push_back(i);
    for (const unsigned position : {1023, 1024, 1025, 2048}) {
        for (const DocId value : {ids[position - 1], ids[position - 1] - 1, search::end_doc}) {
            auto broken = ids;
            broken[position] = value;
            rejected(record("same", broken));
        }
        const auto bytes = record("same", ids);
        for (unsigned missing = 1; missing <= 3; ++missing) {
            rejected(bytes.substr(0, 12 + position * 4 - missing));
        }
    }
    const auto a = run("a", record("same", {0, 255, 256, 1023}));
    for (const DocId value : {1022u, 1023u, search::end_doc}) {
        const auto b = run("b", record("same", {value}));
        expect_error([&] { merge({a, b}); });
    }
    const auto id = run("id", record("same", {}));
    expect_error([&] { merge({id, id}); });
    const auto bound = run("bound", record("same", {0, 1024}));
    expect_error([&] { materialize(bound, 1024); });
    SEARCH_TEST_CHECK(materialize(bound, 1025).postings == 2);
}

void test_builder_publication_failure() {
    TemporaryDirectory temporary;
    const auto blocked = temporary.path() / "target-is-a-directory";
    fs::create_directory(blocked);
    {
        search::IndexBuilder builder(blocked);
        builder.add({"valid", "", "common"});
        expect_error([&] { builder.finish(); });
        expect_error([&] { builder.finish(); });
        expect_error([&] { builder.add({"later", "", "later"}); });
    }
    SEARCH_TEST_CHECK(fs::is_directory(blocked));
    for (const auto& entry : fs::directory_iterator(temporary.path())) {
        SEARCH_TEST_CHECK(entry.path() == blocked);
    }
}

}
