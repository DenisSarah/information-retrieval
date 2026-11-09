#include "tests/test_support.hpp"

#include "search/query.hpp"
#include "search/text.hpp"
#include "index/builder.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace search_tests {

[[noreturn]] void fail(const char* expression, int line, const std::string& detail) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + expression +
                             (detail.empty() ? "" : " (" + detail + ")"));
}

TemporaryDirectory::TemporaryDirectory() {
    static unsigned sequence = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto candidate =
            fs::temp_directory_path() /
            ("mkn-ir-tests-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
        std::error_code error;
        if (fs::create_directory(candidate, error)) {
            path_ = candidate;
            return;
        }
        if (error)
            throw std::runtime_error("cannot create temporary directory: " + error.message());
    }
    throw std::runtime_error("cannot allocate a unique temporary directory");
}

TemporaryDirectory::~TemporaryDirectory() {
    std::error_code error;
    if (!path_.empty())
        fs::remove_all(path_, error);
}

void write_bytes(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create test file");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output)
        throw std::runtime_error("cannot write test file");
}

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read test file");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad())
        throw std::runtime_error("cannot read complete test file");
    return buffer.str();
}

std::vector<DocId> collect(search::Cursor& cursor, std::size_t limit) {
    std::vector<DocId> result;
    while (cursor.doc() != search::end_doc && result.size() < limit) {
        result.push_back(cursor.doc());
        cursor.next();
    }
    return result;
}

namespace {

std::string show_ids(const std::vector<DocId>& ids) {
    std::string result;
    for (const auto id : ids)
        result += std::to_string(id) + " ";
    return result;
}

}

void check_results(const Index& index, const std::string& query,
                   const std::vector<DocId>& expected) {
    auto cursor = search::make_query(index, query);
    SEARCH_TEST_CHECK(cursor != nullptr);
    SEARCH_TEST_CHECK(cursor->doc() == (expected.empty() ? search::end_doc : expected.front()));
    const auto actual = collect(*cursor);
    if (actual != expected) {
        fail("query result equals independent expected result", __LINE__,
             query + "; expected [" + show_ids(expected) + "], got [" + show_ids(actual) + "]");
    }
    SEARCH_TEST_CHECK(cursor->doc() == search::end_doc);
    SEARCH_TEST_CHECK(cursor->next() == search::end_doc);
    SEARCH_TEST_CHECK(cursor->next() == search::end_doc);
    SEARCH_TEST_CHECK(cursor->advance(0) == search::end_doc);
    SEARCH_TEST_CHECK(cursor->advance(search::end_doc) == search::end_doc);
}

Index build_index(const fs::path& file, const std::vector<Document>& documents) {
    search::IndexBuilder builder(file);
    for (const auto& document : documents)
        builder.add(document);
    builder.finish();
    auto index = Index::load(file);
    check_index_documents(index, documents);
    return index;
}

Index make_index(const std::vector<Document>& documents) {
    TemporaryDirectory temporary;
    return build_index(temporary.path() / "fixture.idx", documents);
}

void check_index_documents(const Index& index, const std::vector<Document>& documents) {
    SEARCH_TEST_CHECK(index.document_count() == documents.size());
    std::map<std::string, std::vector<DocId>> expected;
    std::uint64_t text_bytes = 0;
    for (std::size_t id = 0; id < documents.size(); ++id) {
        const auto& document = documents[id];
        const auto actual = index.document(static_cast<DocId>(id));
        SEARCH_TEST_CHECK(actual.external_id == document.external_id);
        SEARCH_TEST_CHECK(actual.title == document.title);
        SEARCH_TEST_CHECK(actual.text == document.text);
        text_bytes += document.text.size();
        const auto tokens = search::tokenize(document.text);
        for (const auto& word : std::set<std::string>(tokens.begin(), tokens.end())) {
            expected[word].push_back(static_cast<DocId>(id));
        }
    }
    SEARCH_TEST_CHECK(index.text_bytes() == text_bytes);
    SEARCH_TEST_CHECK(index.term_count() == expected.size());
    std::uint64_t term_id = 0;
    std::uint64_t posting = 0;
    for (const auto& [word, ids] : expected) {
        const auto term = index.term(term_id++);
        SEARCH_TEST_CHECK(term.word == word);
        SEARCH_TEST_CHECK(term.begin == posting);
        SEARCH_TEST_CHECK(term.count == ids.size());
        for (const auto id : ids)
            SEARCH_TEST_CHECK(index.posting(posting++) == id);
    }
    SEARCH_TEST_CHECK(index.posting_count() == posting);
}

void check_indexes_equal(const Index& left, const Index& right) {
    SEARCH_TEST_CHECK(left.document_count() == right.document_count());
    for (std::size_t i = 0; i < left.document_count(); ++i) {
        SEARCH_TEST_CHECK(left.document(i).external_id == right.document(i).external_id);
        SEARCH_TEST_CHECK(left.document(i).title == right.document(i).title);
        SEARCH_TEST_CHECK(left.document(i).text == right.document(i).text);
    }
    SEARCH_TEST_CHECK(left.term_count() == right.term_count());
    for (std::size_t i = 0; i < left.term_count(); ++i) {
        SEARCH_TEST_CHECK(left.term(i).word == right.term(i).word);
        SEARCH_TEST_CHECK(left.term(i).begin == right.term(i).begin);
        SEARCH_TEST_CHECK(left.term(i).count == right.term(i).count);
    }
    SEARCH_TEST_CHECK(left.posting_count() == right.posting_count());
    for (std::uint64_t i = 0; i < left.posting_count(); ++i) {
        SEARCH_TEST_CHECK(left.posting(i) == right.posting(i));
    }
}

void check_index_invariants(const Index& index) {
    std::uint64_t next_begin = 0;
    std::string previous_term;
    for (std::uint64_t term_id = 0; term_id < index.term_count(); ++term_id) {
        const auto term = index.term(term_id);
        SEARCH_TEST_CHECK(!term.word.empty());
        SEARCH_TEST_CHECK(previous_term.empty() || previous_term < term.word);
        previous_term = term.word;
        SEARCH_TEST_CHECK(term.begin == next_begin);
        SEARCH_TEST_CHECK(term.count > 0);
        SEARCH_TEST_CHECK(term.begin <= index.posting_count());
        SEARCH_TEST_CHECK(term.count <= index.posting_count() - term.begin);
        SEARCH_TEST_CHECK(index.find(term.word).has_value());
        SEARCH_TEST_CHECK(index.find(term.word)->begin == term.begin);
        SEARCH_TEST_CHECK(index.find(term.word)->count == term.count);
        for (std::uint64_t offset = 0; offset < term.count; ++offset) {
            const auto id = index.posting(term.begin + offset);
            SEARCH_TEST_CHECK(id < index.document_count());
            if (offset)
                SEARCH_TEST_CHECK(index.posting(term.begin + offset - 1) < id);
        }
        next_begin += term.count;
    }
    SEARCH_TEST_CHECK(next_begin == index.posting_count());
    SEARCH_TEST_CHECK(!index.find("definitely_unknown_term_123"));
}

Index boolean_fixture() {
    return make_index({
        {"z.md", "", "a a b"},
        {"a.md", "", "a c"},
        {"b.md", "", "b c"},
        {"c.md", "", "a b c"},
        {"d.md", "", "a"},
        {"empty.txt", "", ""},
        {"operators.txt", "", "and or not fox"},
    });
}

}
