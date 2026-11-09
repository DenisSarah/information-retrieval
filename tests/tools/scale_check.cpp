#include "search/index.hpp"
#include "search/query.hpp"
#include "search/cursor.hpp"

#include <zlib.h>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t count_from(std::string_view input) {
    std::uint64_t count = 0;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), count);
    if (input.empty() || result.ec != std::errc{} || result.ptr != input.data() + input.size() ||
        !count || count > search::end_doc)
        throw std::runtime_error("invalid COUNT");
    return count;
}

void generate(const std::filesystem::path& path, std::uint64_t count) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    gzFile out = gzopen(path.c_str(), "wb1");
    if (!out)
        throw std::runtime_error("cannot create fixture");
    bool good = true;
    for (std::uint64_t i = 0; i < count && good; ++i) {
        std::string body = "all";
        if (i % 2 == 0)
            body += " even";
        if (i % 5 == 0)
            body += " fifth";
        if (i + 1 == count)
            body += " last";
        const std::string line = "{\"docid\":\"scale_" + std::to_string(i) +
                                 "\",\"title\":\"\",\"headings\":\"\",\"body\":\"" + body + "\"}\n";
        good = gzwrite(out, line.data(), static_cast<unsigned>(line.size())) ==
               static_cast<int>(line.size());
    }
    const auto closed = gzclose(out);
    if (!good || closed != Z_OK)
        throw std::runtime_error("cannot write fixture");
    std::cout << "Generated " << count << " synthetic documents: " << path << '\n';
}

void check(const std::filesystem::path& path, std::uint64_t count) {
    const auto index = search::Index::load(path);
    if (index.document_count() != count)
        throw std::runtime_error("wrong document count");
    index.verify();
    for (const auto& word : {"all", "even", "fifth", "last", "unknown"}) {
        const auto term = index.find(word);
        const std::uint64_t expected = word == std::string_view("all")     ? count
                                       : word == std::string_view("even")  ? (count + 1) / 2
                                       : word == std::string_view("fifth") ? (count + 4) / 5
                                       : word == std::string_view("last")  ? 1
                                                                           : 0;
        if ((term ? term->count : 0) != expected)
            throw std::runtime_error("wrong df");
    }
    auto cursor = search::make_query(index, "all AND NOT (even OR fifth)");
    for (std::uint64_t i = 0; i < count; ++i) {
        if (i % 2 == 0 || i % 5 == 0)
            continue;
        if (cursor->doc() != i)
            throw std::runtime_error("wrong boolean result");
        cursor->next();
    }
    if (cursor->doc() != search::end_doc)
        throw std::runtime_error("unexpected extra result");
    auto last = search::make_query(index, "last");
    if (last->doc() != count - 1)
        throw std::runtime_error("last document missing");
    for (const auto id : {std::uint64_t(0), count / 2, count - 1}) {
        const auto document = index.document(static_cast<search::DocId>(id));
        if (document.external_id != "scale_" + std::to_string(id))
            throw std::runtime_error("wrong external ID");
        auto seek = search::make_query(index, "all");
        if (seek->advance(static_cast<search::DocId>(id)) != id)
            throw std::runtime_error("wrong advance");
    }
    std::cout << "Checked " << count
              << " synthetic documents, full structure, df, boolean results, IDs and advance.\n";
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: scale_check generate|check PATH COUNT");
        const auto count = count_from(argv[3]);
        if (std::string_view(argv[1]) == "generate")
            generate(argv[2], count);
        else if (std::string_view(argv[1]) == "check")
            check(argv[2], count);
        else
            throw std::runtime_error("unknown command");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
