#pragma once

namespace search_tests {

void test_tokenizer();
void test_index_and_boolean_queries();
void test_invalid_queries_and_limits();
void test_cursor_contract();
void test_posting_advance_windows();
void test_empty_documents_and_invalid_input();
void test_serialization_and_corruption();
void test_64bit_file_offsets();
void test_random_boolean_oracle();
void test_external_builder();
void test_builder_buffer_boundaries();
void test_builder_run_blocks();
void test_builder_publication_failure();
void test_builder_inplace_shift();
void test_builder_independent_v2_bytes();
void test_msmarco_import();

}
