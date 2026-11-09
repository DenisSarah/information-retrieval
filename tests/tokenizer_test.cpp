#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include "search/text.hpp"

#include <string>
#include <vector>

namespace search_tests {

void test_tokenizer() {
    const std::vector<std::string> expected{"hello", "world", "42",  "c",        "foo",   "bar",
                                            "еж",    "еж",    "еж",  "еж",       "й",     "й",
                                            "земля", "123",   "абв", "latin123", "привет"};
    SEARCH_TEST_CHECK(search::tokenize("Hello, WORLD_42 C++ foo-bar ЁЖ ёж ЕЖ еЖ Й й "
                                       "ЗЕМЛЯ 123 абв Latin123 ПрИвЕт") == expected);
    SEARCH_TEST_CHECK(search::tokenize("a漢b🙂c") == std::vector<std::string>({"a", "b", "c"}));
    SEARCH_TEST_CHECK(search::tokenize("A\u00a0B…C\nД") ==
                      std::vector<std::string>({"a", "b", "c", "д"}));
    SEARCH_TEST_CHECK(search::tokenize(std::string("a\0b", 3)) ==
                      std::vector<std::string>({"a", "b"}));
    SEARCH_TEST_CHECK(search::tokenize(std::string("a") + "\xf4\x8f\xbf\xbf" + "b") ==
                      std::vector<std::string>({"a", "b"}));
    SEARCH_TEST_CHECK(search::tokenize("").empty());
    SEARCH_TEST_CHECK(search::tokenize("--- ; 🙂 漢").empty());

    const std::vector<std::string> malformed{
        "\x80",         "\xc0\xaf",     "\xc1\xbf",         "\xc2",
        "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
        "\xff",         "ok\xe2\x82",   "\xe2\x28\xa1",     "\xf0\x9f\x92"};
    for (const auto& bytes : malformed)
        expect_error([&] { search::tokenize(bytes); });
}

}
