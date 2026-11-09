#pragma once

#include "search/document.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace search {

struct BuildOptions {
    std::size_t memory_bytes = 256 * 1024 * 1024;
    std::size_t merge_fan_in = 32;
};

struct BuildStats {
    std::uint64_t documents = 0;
    std::uint64_t terms = 0;
    std::uint64_t postings = 0;
    std::uint64_t text_bytes = 0;
};

class IndexBuilder {
public:
    explicit IndexBuilder(std::filesystem::path output, BuildOptions options = {});
    ~IndexBuilder();
    IndexBuilder(const IndexBuilder&) = delete;
    IndexBuilder& operator=(const IndexBuilder&) = delete;

    void add(Document document);
    BuildStats finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
