#include "index/assembly.hpp"

#include "index/binary_io.hpp"
#include "index/builder_runs.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace search::detail {
namespace {

namespace fs = std::filesystem;
constexpr std::size_t buffer_bytes = 64 * 1024;

std::streamoff stream_offset(std::uint64_t value) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("Смещение индекса превышает диапазон файлового потока");
    }
    return static_cast<std::streamoff>(value);
}

void seek_output(std::fstream& stream, std::uint64_t position) {
    stream.clear();
    stream.seekp(stream_offset(position), std::ios::beg);
    builder_check(stream);
}

void consume_section(const fs::path& path, std::ostream& output, std::uint64_t expected) {
    if (fs::file_size(path) != expected)
        throw std::runtime_error("Неверный размер временной секции индекса");
    std::ifstream input(path, std::ios::binary);
    builder_check(input);
    std::array<char, buffer_bytes> buffer{};
    while (expected) {
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), expected));
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        builder_check(input);
        output.write(buffer.data(), static_cast<std::streamsize>(count));
        builder_check(output);
        expected -= count;
    }
    input.close();
    fs::remove(path);
}

}

void shift_builder_document_data(const fs::path& file, std::uint64_t bytes, std::uint64_t delta) {
    if (fs::file_size(file) != bytes)
        throw std::runtime_error("Неверный размер временных документов");
    const auto length = builder_add(bytes, delta);
    stream_offset(length);
    fs::resize_file(file, length);
    std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
    builder_check(stream);
    std::array<char, buffer_bytes> buffer{};
    auto remaining = bytes;
    while (remaining) {
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const auto source = remaining - count;
        stream.clear();
        stream.seekg(stream_offset(source), std::ios::beg);
        builder_check(stream);
        stream.read(buffer.data(), static_cast<std::streamsize>(count));
        builder_check(stream);
        seek_output(stream, builder_add(source, delta));
        stream.write(buffer.data(), static_cast<std::streamsize>(count));
        builder_check(stream);
        remaining = source;
    }
    stream.close();
    builder_check(stream);
}

void assemble_builder_file(const BuilderSections& sections, const BuildStats& stats,
                           std::uint64_t document_bytes, std::uint64_t term_bytes) {
    const auto doc_offsets_size = builder_multiply(builder_add(stats.documents, 1), 8);
    const auto term_offsets_size = builder_multiply(builder_add(stats.terms, 1), 8);
    const auto posting_size = builder_multiply(stats.postings, 4);
    const std::uint64_t doc_offsets_pos = 96;
    const auto doc_data_pos = builder_add(doc_offsets_pos, doc_offsets_size);
    const auto term_offsets_pos = builder_add(doc_data_pos, document_bytes);
    const auto term_data_pos = builder_add(term_offsets_pos, term_offsets_size);
    const auto postings_pos = builder_add(term_data_pos, term_bytes);
    const auto end_pos = builder_add(postings_pos, posting_size);
    stream_offset(end_pos);

    shift_builder_document_data(sections.document_data, document_bytes, doc_data_pos);
    std::fstream output(sections.document_data, std::ios::in | std::ios::out | std::ios::binary);
    builder_check(output);
    seek_output(output, 0);
    output.write("SIRIDX02", 8);
    write_number(output, 2, 4);
    write_number(output, 1, 4);
    for (const auto value : std::array<std::uint64_t, 10>{
             stats.documents, stats.terms, stats.postings, stats.text_bytes, doc_offsets_pos,
             doc_data_pos, term_offsets_pos, term_data_pos, postings_pos, end_pos}) {
        write_number(output, value, 8);
    }
    builder_check(output);
    consume_section(sections.document_offsets, output, doc_offsets_size);
    seek_output(output, term_offsets_pos);
    consume_section(sections.term_offsets, output, term_offsets_size);
    consume_section(sections.term_data, output, term_bytes);
    consume_section(sections.postings, output, posting_size);
    output.close();
    builder_check(output);
    if (fs::file_size(sections.document_data) != end_pos) {
        throw std::runtime_error("Неверная длина готового индекса");
    }
}

}
