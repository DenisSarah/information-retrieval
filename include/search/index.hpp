#pragma once

#include "search/document.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace search {

struct Term {
    std::string word;
    std::uint64_t begin;
    std::uint32_t count;
};

class Index {
public:
    static Index load(const std::filesystem::path& file);
    void verify() const;

    std::uint64_t document_count() const { return documents_; }
    std::uint64_t term_count() const { return terms_; }
    std::uint64_t posting_count() const { return postings_; }
    std::uint64_t text_bytes() const { return text_bytes_; }
    Document document(DocId id) const;
    Term term(std::uint64_t position) const;
    DocId posting(std::uint64_t position) const;
    std::optional<Term> find(std::string_view word) const;

private:
    Index() = default;
    void validate_layout();
    std::uint64_t number(std::uint64_t offset, unsigned width) const;
    std::string string(std::uint64_t& offset, std::uint64_t end) const;
    std::pair<std::uint64_t, std::uint64_t> record(std::uint64_t table, std::uint64_t position,
                                                   std::uint64_t section, std::uint64_t end) const;

    std::shared_ptr<const char> data_;
    std::uint64_t size_ = 0;
    std::uint64_t documents_ = 0, terms_ = 0, postings_ = 0, text_bytes_ = 0;
    std::uint64_t doc_offsets_ = 0, doc_data_ = 0;
    std::uint64_t term_offsets_ = 0, term_data_ = 0, postings_data_ = 0;
};

}
