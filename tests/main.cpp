#include "tests/testcases.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"tokenizer and strict UTF-8", search_tests::test_tokenizer},
        {"index and boolean queries", search_tests::test_index_and_boolean_queries},
        {"invalid queries and resource limits", search_tests::test_invalid_queries_and_limits},
        {"cursor contract and limited traversal", search_tests::test_cursor_contract},
        {"posting advance windows at powers of two", search_tests::test_posting_advance_windows},
        {"empty documents and invalid input", search_tests::test_empty_documents_and_invalid_input},
        {"deterministic serialization and corruption",
         search_tests::test_serialization_and_corruption},
        {"64-bit file offsets beyond 4 GiB", search_tests::test_64bit_file_offsets},
        {"3200 random queries against independent boolean oracle",
         search_tests::test_random_boolean_oracle},
        {"external builder and mapped index", search_tests::test_external_builder},
        {"builder posting buffer boundaries and multipass",
         search_tests::test_builder_buffer_boundaries},
        {"temporary run blocks and corruption boundaries", search_tests::test_builder_run_blocks},
        {"builder publication failure and cleanup", search_tests::test_builder_publication_failure},
        {"in-place document shift and overlap boundaries",
         search_tests::test_builder_inplace_shift},
        {"independent v2 wire format", search_tests::test_builder_independent_v2_bytes},
        {"streaming MS MARCO JSONL and gzip import", search_tests::test_msmarco_import},
    };
    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << " test groups passed, " << failed << " failed.\n";
    return failed ? 1 : 0;
}
