#include "tests/test_support.hpp"
#include "tests/testcases.hpp"
#include "search/query.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace search_tests {

void test_serialization_and_corruption() {
    TemporaryDirectory temporary;
    const std::vector<Document> documents{
        {"z.md", "Заголовок", "alpha beta Ёж"},
        {"a.md", "Title", "beta beta gamma"},
        {"e.md", "", ""},
    };
    const auto first_path = temporary.path() / "first.index";
    const auto second_path = temporary.path() / "second.index";
    const auto index = build_index(first_path, documents);
    build_index(second_path, documents);
    const auto bytes = read_bytes(first_path);
    SEARCH_TEST_CHECK(bytes == read_bytes(second_path));
    const auto loaded = Index::load(first_path);
    loaded.verify();
    check_indexes_equal(index, loaded);
    check_index_invariants(loaded);
    write_bytes(second_path, bytes);
    check_index_documents(Index::load(second_path), documents);
    SEARCH_TEST_CHECK(bytes == read_bytes(second_path));
    for (const auto& query :
         {"alpha", "beta", "ЁЖ", "alpha OR gamma", "NOT beta", "(alpha OR gamma) AND NOT еж"}) {
        auto cursor = search::make_query(index, query);
        check_results(loaded, query, collect(*cursor));
    }

    const auto bad_path = temporary.path() / "bad.index";
    const auto reject = [&](const std::string& invalid_bytes) {
        write_bytes(bad_path, invalid_bytes);
        expect_error([&] { Index::load(bad_path).verify(); });
    };
    const auto read_number = [&](std::size_t offset, unsigned width) {
        SEARCH_TEST_CHECK(offset <= bytes.size() && width <= bytes.size() - offset);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) {
            value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
        }
        return value;
    };
    const auto put_number = [](std::string& target, std::size_t offset, std::uint64_t value,
                               unsigned width) {
        SEARCH_TEST_CHECK(offset <= target.size() && width <= target.size() - offset);
        for (unsigned i = 0; i < width; ++i)
            target[offset + i] = static_cast<char>(value >> (8 * i));
    };
    const auto corrupt_number = [&](std::size_t offset, std::uint64_t value, unsigned width) {
        auto bad = bytes;
        put_number(bad, offset, value, width);
        reject(bad);
    };
    SEARCH_TEST_CHECK(bytes.substr(0, 8) == "SIRIDX02");
    SEARCH_TEST_CHECK(read_number(16, 8) == index.document_count());
    SEARCH_TEST_CHECK(read_number(24, 8) == index.term_count());
    SEARCH_TEST_CHECK(read_number(32, 8) == index.posting_count());
    SEARCH_TEST_CHECK(read_number(40, 8) == index.text_bytes());
    const auto doc_offsets = read_number(48, 8);
    const auto doc_data = read_number(56, 8);
    const auto term_offsets = read_number(64, 8);
    const auto term_data = read_number(72, 8);
    const auto postings = read_number(80, 8);
    SEARCH_TEST_CHECK(read_number(88, 8) == bytes.size());
    SEARCH_TEST_CHECK(postings + 4 * index.posting_count() == bytes.size());
    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    reject(bad_magic);
    reject(bytes + '\0');
    corrupt_number(8, 99, 4);
    corrupt_number(12, 99, 4);
    for (const auto offset : {16, 24, 32, 40, 48, 56, 64, 72, 80, 88}) {
        corrupt_number(offset, std::numeric_limits<std::uint64_t>::max(), 8);
    }
    corrupt_number(doc_offsets, 1, 8);
    corrupt_number(doc_offsets + 8, 0, 8);
    corrupt_number(term_offsets, 1, 8);
    corrupt_number(term_offsets + 8, 0, 8);
    corrupt_number(doc_data, search::end_doc, 4);
    auto cursor = doc_data;
    for (unsigned i = 0; i < 3; ++i) {
        const auto length = read_number(cursor, 4);
        SEARCH_TEST_CHECK(length != 0);
        auto bad_utf8 = bytes;
        bad_utf8[cursor + 4] = static_cast<char>(0xff);
        reject(bad_utf8);
        cursor += 4 + length;
    }
    const auto first_term_length = read_number(term_data, 4);
    SEARCH_TEST_CHECK(first_term_length > 1);
    auto uppercase = bytes;
    uppercase[term_data + 4] = 'A';
    reject(uppercase);
    auto split_word = bytes;
    split_word[term_data + 5] = ' ';
    reject(split_word);
    auto unordered = bytes;
    unordered.replace(term_data + 4, first_term_length, first_term_length, 'z');
    reject(unordered);
    corrupt_number(term_data + 4 + first_term_length, 1, 8);
    corrupt_number(term_data + 12 + first_term_length, 0, 4);
    corrupt_number(term_data + 12 + first_term_length, index.document_count() + 1, 4);
    corrupt_number(postings, search::end_doc, 4);
    corrupt_number(postings, index.document_count(), 4);
    for (std::uint64_t i = 0; i < index.term_count(); ++i) {
        const auto term = index.term(i);
        if (term.count < 2)
            continue;
        const auto start = postings + 4 * term.begin;
        corrupt_number(start + 4, read_number(start, 4), 4);
        auto reversed = bytes;
        put_number(reversed, start, read_number(start + 4, 4), 4);
        put_number(reversed, start + 4, read_number(start, 4), 4);
        reject(reversed);
        break;
    }
    for (std::size_t length = 0; length < bytes.size(); ++length)
        reject(bytes.substr(0, length));
    expect_error([&] { loaded.document(static_cast<DocId>(index.document_count())); });
    expect_error([&] { loaded.term(index.term_count()); });
    expect_error([&] { loaded.posting(index.posting_count()); });
    expect_error([&] { Index::load(temporary.path() / "absent.index"); });
}

}
