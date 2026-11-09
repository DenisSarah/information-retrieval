#include "tests/test_support.hpp"
#include "index/assembly.hpp"
#include "index/builder.hpp"

#include <cstddef>
#include <cstdint>
#include <ios>
#include <limits>
#include <string>

namespace search_tests {

void test_builder_inplace_shift() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "shift-data";
    for (const std::size_t length : {0u, 1u, 31u, 65535u, 65536u, 65537u, 131109u}) {
        std::string original(length, '\0');
        for (std::size_t i = 0; i < original.size(); ++i) {
            original[i] = static_cast<char>((i * 31 + i / 257) & 255);
        }
        for (const std::uint64_t delta : {0u, 1u, 17u, 104u, 65535u, 65536u, 65537u}) {
            write_bytes(file, original);
            search::detail::shift_builder_document_data(file, original.size(), delta);
            const auto shifted = read_bytes(file);
            SEARCH_TEST_CHECK(shifted.size() == original.size() + delta);
            SEARCH_TEST_CHECK(shifted.substr(static_cast<std::size_t>(delta)) == original);
        }
    }
    write_bytes(file, "abc");
    expect_error([&] { search::detail::shift_builder_document_data(file, 4, 1); });
    SEARCH_TEST_CHECK(read_bytes(file) == "abc");
    expect_error([&] {
        search::detail::shift_builder_document_data(file, 3,
                                                    std::numeric_limits<std::uint64_t>::max());
    });
    SEARCH_TEST_CHECK(read_bytes(file) == "abc");
    expect_error([&] {
        search::detail::shift_builder_document_data(
            file, 3, static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()));
    });
    SEARCH_TEST_CHECK(read_bytes(file) == "abc");
}

void test_builder_independent_v2_bytes() {
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "expected-wire.idx";
    search::BuildOptions options;
    options.memory_bytes = 1;
    options.merge_fan_in = 2;
    {
        search::IndexBuilder builder(file, options);
        builder.add({"a", "A", "cat dog"});
        builder.add({"b", "B", "dog"});
        const auto stats = builder.finish();
        SEARCH_TEST_CHECK(stats.documents == 2 && stats.terms == 2);
        SEARCH_TEST_CHECK(stats.postings == 3 && stats.text_bytes == 10);
    }
    std::string expected = "SIRIDX02";
    const auto number = [&](std::uint64_t value, unsigned width) {
        for (unsigned byte = 0; byte < width; ++byte) {
            expected.push_back(static_cast<char>((value >> (8 * byte)) & 255));
        }
    };
    const auto string = [&](const std::string& value) {
        number(value.size(), 4);
        expected += value;
    };
    number(2, 4);
    number(1, 4);
    for (const std::uint64_t value : {2u, 2u, 3u, 10u, 96u, 120u, 158u, 182u, 220u, 232u}) {
        number(value, 8);
    }
    for (const auto offset : {0u, 21u, 38u})
        number(offset, 8);
    string("a");
    string("A");
    string("cat dog");
    string("b");
    string("B");
    string("dog");
    for (const auto offset : {0u, 19u, 38u})
        number(offset, 8);
    string("cat");
    number(0, 8);
    number(1, 4);
    string("dog");
    number(1, 8);
    number(2, 4);
    for (const auto id : {0u, 0u, 1u})
        number(id, 4);
    SEARCH_TEST_CHECK(expected.size() == 232);
    SEARCH_TEST_CHECK(read_bytes(file) == expected);
    Index::load(file).verify();
}

}
