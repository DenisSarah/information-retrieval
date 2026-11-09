#pragma once

#include "search/document.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace search::detail {

inline constexpr std::uint64_t builder_record_limit = 64 * 1024 * 1024;
using BuilderTerms = std::map<std::string, std::vector<DocId>>;

struct BuilderRun {
    std::filesystem::path path;
    std::uint64_t max_record = 0;
};

struct BuilderTermStats {
    std::uint64_t terms = 0;
    std::uint64_t postings = 0;
    std::uint64_t bytes = 0;
};

std::uint64_t builder_add(std::uint64_t a, std::uint64_t b);
std::uint64_t builder_multiply(std::uint64_t a, std::uint64_t b);
void builder_check(const std::ios& stream);
void builder_close(std::ofstream& stream);
std::ofstream builder_output(const std::filesystem::path& file);
BuilderRun write_builder_run(const std::filesystem::path& file, const BuilderTerms& terms,
                             std::vector<std::string>& ids);
BuilderRun merge_builder_runs(const std::filesystem::path& file,
                              const std::vector<BuilderRun>& runs);
BuilderTermStats materialize_builder_terms(const BuilderRun* run, std::uint64_t documents,
                                           const std::filesystem::path& offsets,
                                           const std::filesystem::path& records,
                                           const std::filesystem::path& postings);

}
