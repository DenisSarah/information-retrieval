#pragma once

#include "index/builder.hpp"

#include <cstdint>
#include <filesystem>

namespace search::detail {

struct BuilderSections {
    std::filesystem::path document_data;
    std::filesystem::path document_offsets;
    std::filesystem::path term_offsets;
    std::filesystem::path term_data;
    std::filesystem::path postings;
};

void shift_builder_document_data(const std::filesystem::path& file, std::uint64_t bytes,
                                 std::uint64_t delta);
void assemble_builder_file(const BuilderSections& sections, const BuildStats& stats,
                           std::uint64_t document_bytes, std::uint64_t term_bytes);

}
