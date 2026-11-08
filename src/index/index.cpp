#include "search/index.hpp"

#include "index/binary_io.hpp"
#include "search/text.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace search {
namespace {

std::uint64_t table_end(std::uint64_t start, std::uint64_t count, std::uint64_t file_size) {
    if (start > file_size || count == std::numeric_limits<std::uint64_t>::max() ||
        count + 1 > (file_size - start) / 8)
        detail::invalid_index();
    return start + (count + 1) * 8;
}

}

Index Index::load(const std::filesystem::path& file) {
    struct Descriptor {
        int value;
        ~Descriptor() {
            if (value >= 0)
                ::close(value);
        }
    } descriptor{::open(file.c_str(), O_RDONLY)};
    if (descriptor.value < 0)
        throw std::runtime_error("Не удалось открыть индекс: " + file.string());
    struct stat status{};
    if (::fstat(descriptor.value, &status) != 0) {
        throw std::runtime_error("Не удалось определить размер индекса: " + file.string());
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 96)
        detail::invalid_index();
    Index result;
    result.size_ = static_cast<std::uint64_t>(status.st_size);
    if (result.size_ > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Индекс слишком велик для адресного пространства процесса");
    }
    void* address = ::mmap(nullptr, static_cast<std::size_t>(result.size_), PROT_READ, MAP_PRIVATE,
                           descriptor.value, 0);
    if (address == MAP_FAILED)
        throw std::runtime_error("Не удалось отобразить индекс: " + file.string());
    result.data_.reset(static_cast<const char*>(address), [size = result.size_](const char* data) {
        ::munmap(const_cast<char*>(data), static_cast<std::size_t>(size));
    });
    result.validate_layout();
    return result;
}

std::uint64_t Index::number(std::uint64_t offset, unsigned width) const {
    if (width > 8 || offset > size_ || width > size_ - offset)
        detail::invalid_index();
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i) {
        result |= std::uint64_t(static_cast<unsigned char>(data_.get()[offset + i])) << (8 * i);
    }
    return result;
}

void Index::validate_layout() {
    if (std::memcmp(data_.get(), "SIRIDX02", 8) != 0 || number(8, 4) != 2 || number(12, 4) != 1)
        detail::invalid_index();
    documents_ = number(16, 8);
    terms_ = number(24, 8);
    postings_ = number(32, 8);
    text_bytes_ = number(40, 8);
    doc_offsets_ = number(48, 8);
    doc_data_ = number(56, 8);
    term_offsets_ = number(64, 8);
    term_data_ = number(72, 8);
    postings_data_ = number(80, 8);
    if (documents_ > end_doc || doc_offsets_ != 96 || number(88, 8) != size_ ||
        doc_data_ != table_end(doc_offsets_, documents_, size_) || term_offsets_ < doc_data_ ||
        term_offsets_ > size_ || term_data_ != table_end(term_offsets_, terms_, size_) ||
        postings_data_ < term_data_ || postings_data_ > size_ ||
        postings_ != (size_ - postings_data_) / 4 || (size_ - postings_data_) % 4)
        detail::invalid_index();
    const auto doc_bytes = term_offsets_ - doc_data_;
    const auto term_bytes = postings_data_ - term_data_;
    if (documents_ > doc_bytes / 12 || terms_ > term_bytes / 17 || text_bytes_ > doc_bytes ||
        terms_ > postings_ || (!terms_ && postings_) || (!documents_ && (terms_ || postings_)))
        detail::invalid_index();
    if (documents_ && terms_ <= std::numeric_limits<std::uint64_t>::max() / documents_ &&
        postings_ > documents_ * terms_)
        detail::invalid_index();
    if (number(doc_offsets_, 8) || number(doc_offsets_ + documents_ * 8, 8) != doc_bytes ||
        number(term_offsets_, 8) || number(term_offsets_ + terms_ * 8, 8) != term_bytes)
        detail::invalid_index();
}

std::pair<std::uint64_t, std::uint64_t> Index::record(std::uint64_t table, std::uint64_t position,
                                                      std::uint64_t section,
                                                      std::uint64_t end) const {
    const auto begin = number(table + position * 8, 8);
    const auto finish = number(table + (position + 1) * 8, 8);
    if (begin > finish || finish > end - section)
        detail::invalid_index();
    return {section + begin, section + finish};
}

std::string Index::string(std::uint64_t& offset, std::uint64_t end) const {
    if (offset > end || end - offset < 4)
        detail::invalid_index();
    const auto length = number(offset, 4);
    offset += 4;
    if (length > end - offset)
        detail::invalid_index();
    std::string result(data_.get() + offset, static_cast<std::size_t>(length));
    offset += length;
    validate_utf8(result);
    return result;
}

Document Index::document(DocId id) const {
    if (id == end_doc || id >= documents_)
        throw std::runtime_error("DOC_ID вне диапазона документов индекса");
    const auto bounds = record(doc_offsets_, id, doc_data_, term_offsets_);
    auto position = bounds.first;
    Document result{string(position, bounds.second), string(position, bounds.second),
                    string(position, bounds.second)};
    if (position != bounds.second || result.external_id.empty())
        detail::invalid_index();
    return result;
}

Term Index::term(std::uint64_t position) const {
    if (position >= terms_)
        throw std::runtime_error("Номер терма вне диапазона индекса");
    const auto bounds = record(term_offsets_, position, term_data_, postings_data_);
    auto offset = bounds.first;
    auto word = string(offset, bounds.second);
    if (word.empty() || bounds.second - offset != 12)
        detail::invalid_index();
    const auto begin = number(offset, 8);
    const auto count = static_cast<std::uint32_t>(number(offset + 8, 4));
    if (!count || count > documents_ || begin > postings_ || count > postings_ - begin)
        detail::invalid_index();
    return {std::move(word), begin, count};
}

DocId Index::posting(std::uint64_t position) const {
    if (position >= postings_)
        throw std::runtime_error("Позиция posting вне диапазона индекса");
    const auto id = static_cast<DocId>(number(postings_data_ + position * 4, 4));
    if (id == end_doc || id >= documents_)
        detail::invalid_index();
    return id;
}

std::optional<Term> Index::find(std::string_view word) const {
    std::uint64_t low = 0, high = term_count();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (term(middle).word < word)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == term_count())
        return std::nullopt;
    auto result = term(low);
    return result.word == word ? std::optional<Term>(std::move(result)) : std::nullopt;
}

void Index::verify() const {
    std::uint64_t total_text = 0;
    for (std::uint64_t id = 0; id < document_count(); ++id) {
        const auto value = document(static_cast<DocId>(id));
        if (value.text.size() > std::numeric_limits<std::uint64_t>::max() - total_text) {
            detail::invalid_index();
        }
        total_text += value.text.size();
    }
    if (total_text != text_bytes())
        detail::invalid_index();
    std::string previous_word;
    std::uint64_t expected_begin = 0;
    for (std::uint64_t position = 0; position < term_count(); ++position) {
        const auto value = term(position);
        const auto words = tokenize(value.word);
        if (words.size() != 1 || words[0] != value.word ||
            (position && previous_word >= value.word) || value.begin != expected_begin)
            detail::invalid_index();
        DocId previous = 0;
        for (std::uint64_t i = 0; i < value.count; ++i) {
            const auto id = posting(value.begin + i);
            if (i && id <= previous)
                detail::invalid_index();
            previous = id;
        }
        expected_begin += value.count;
        previous_word = value.word;
    }
    if (expected_begin != posting_count())
        detail::invalid_index();
}

}
