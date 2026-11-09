#include "search/cursor.hpp"
#include "search/document.hpp"
#include "search/index.hpp"
#include "search/query.hpp"
#include "search/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Words = std::unordered_set<std::string>;
constexpr std::size_t head_limit = 20;
constexpr std::size_t sample_limit = 32;
constexpr std::array<std::string_view, 6> queries = {
    "search",
    "information retrieval",
    "neural AND (search OR retrieval)",
    "search AND NOT image",
    "(search OR retrieval) AND NOT image",
    "NOT search",
};

struct Sample {
    search::DocId id;
    std::array<bool, queries.size()> expected;
};

struct Head {
    std::vector<search::DocId> ids;
    double milliseconds;
};

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::uint64_t parse_count(std::string_view value) {
    std::uint64_t count = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
    require(!value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
            "EXPECTED_D must be a nonnegative decimal integer");
    require(count <= search::end_doc, "EXPECTED_D exceeds the DocId capacity");
    return count;
}

bool expected_match(std::size_t query, const Words& words) {
    const auto has = [&words](const char* word) { return words.count(word) != 0; };
    const bool search = has("search");
    const bool retrieval = has("retrieval");
    const bool image = has("image");
    switch (query) {
    case 0:
        return search;
    case 1:
        return has("information") && retrieval;
    case 2:
        return has("neural") && (search || retrieval);
    case 3:
        return search && !image;
    case 4:
        return (search || retrieval) && !image;
    case 5:
        return !search;
    default:
        throw std::runtime_error("invalid checker predicate");
    }
}

std::vector<Sample> prepare_samples(const search::Index& index) {
    std::vector<Sample> samples;
    const std::uint64_t count = index.document_count();
    if (count == 0)
        return samples;
    for (std::size_t i = 0; i < sample_limit; ++i) {
        const auto id = static_cast<search::DocId>((count - 1) * i / (sample_limit - 1));
        if (!samples.empty() && samples.back().id == id)
            continue;
        const auto document = index.document(id);
        auto tokens = search::tokenize(document.text);
        Words words;
        words.reserve(tokens.size());
        for (auto& token : tokens)
            words.insert(std::move(token));
        Sample sample{id, {}};
        for (std::size_t query = 0; query < queries.size(); ++query) {
            sample.expected[query] = expected_match(query, words);
        }
        samples.push_back(sample);
    }
    return samples;
}

void check_id(search::DocId id, std::uint64_t count, std::string_view query) {
    require(id != search::end_doc && id < count,
            "out-of-range result for query: " + std::string(query));
}

Head query_head(const search::Index& index, std::string_view query) {
    const auto start = Clock::now();
    auto cursor = search::make_query(index, query);
    std::vector<search::DocId> ids;
    ids.reserve(head_limit);
    while (cursor->doc() != search::end_doc && ids.size() < head_limit) {
        const auto id = cursor->doc();
        check_id(id, index.document_count(), query);
        require(ids.empty() || ids.back() < id,
                "results are not strictly increasing for query: " + std::string(query));
        ids.push_back(id);
        if (ids.size() < head_limit)
            cursor->next();
    }
    const double milliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {std::move(ids), milliseconds};
}

template <class Predicate>
void check_samples(const search::Index& index, std::string_view query,
                   const std::vector<Sample>& samples, Predicate expected) {
    auto cursor = search::make_query(index, query);
    search::DocId previous = 0;
    bool have_previous = false;
    for (const auto& sample : samples) {
        const auto found = cursor->advance(sample.id);
        if (found != search::end_doc) {
            check_id(found, index.document_count(), query);
            require(found >= sample.id && (!have_previous || found >= previous),
                    "advance moved backwards for query: " + std::string(query));
        } else {
            require(cursor->doc() == search::end_doc,
                    "advance returned end without exhausting cursor");
        }
        require((found == sample.id) == expected(sample),
                "independent text predicate disagrees at DocId " + std::to_string(sample.id) +
                    " for query: " + std::string(query));
        previous = found;
        have_previous = true;
    }
    require(cursor->advance(search::end_doc) == search::end_doc &&
                cursor->next() == search::end_doc && cursor->advance(0) == search::end_doc,
            "cursor end is not permanent for query: " + std::string(query));
}

void print_head(std::string_view query, const Head& head) {
    std::cout << "query=" << std::quoted(std::string(query)) << " head=";
    for (std::size_t i = 0; i < head.ids.size(); ++i) {
        if (i != 0)
            std::cout << ',';
        std::cout << head.ids[i];
    }
    if (head.ids.empty())
        std::cout << "(empty)";
    std::cout << " parse_and_first20_ms=" << std::fixed << std::setprecision(3) << head.milliseconds
              << '\n';
}

std::string absent_word(const search::Index& index) {
    std::string word = "zzzxqvcontrolmsmarco20261025";
    while (index.find(word))
        word.push_back('z');
    require(search::tokenize(word) == std::vector<std::string>{word},
            "unknown control term is not one normalized token");
    return word;
}

void check_unknown(const search::Index& index, const std::vector<Sample>& samples,
                   const Head& base) {
    const auto unknown = absent_word(index);
    require(!index.find(unknown), "control term unexpectedly exists in dictionary");
    const std::array<std::string, 3> identities = {
        "search OR " + unknown,
        "search AND " + unknown,
        "search AND NOT " + unknown,
    };
    for (std::size_t i = 0; i < identities.size(); ++i) {
        const auto head = query_head(index, identities[i]);
        const auto expected = i == 1 ? std::vector<search::DocId>{} : base.ids;
        require(head.ids == expected,
                "unknown-term identity disagrees for query: " + identities[i]);
        check_samples(index, identities[i], samples,
                      [i](const Sample& sample) { return i != 1 && sample.expected[0]; });
        print_head(identities[i], head);
    }

    const std::string complement = "NOT " + unknown;
    const auto head = query_head(index, complement);
    std::vector<search::DocId> expected;
    for (std::uint64_t id = 0; id < std::min<std::uint64_t>(head_limit, index.document_count());
         ++id) {
        expected.push_back(static_cast<search::DocId>(id));
    }
    require(head.ids == expected, "NOT unknown does not enumerate the collection head");
    check_samples(index, complement, samples, [](const Sample&) { return true; });
    auto cursor = search::make_query(index, complement);
    if (index.document_count() != 0) {
        const auto last = static_cast<search::DocId>(index.document_count() - 1);
        require(cursor->advance(last) == last, "NOT unknown misses the last document");
    }
    require(cursor->advance(static_cast<search::DocId>(index.document_count())) ==
                    search::end_doc &&
                cursor->next() == search::end_doc && cursor->advance(0) == search::end_doc,
            "NOT unknown does not terminate permanently after the collection");
    print_head(complement, head);
}

}

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: " << argv[0] << " INDEX EXPECTED_D\n";
            return 2;
        }
        const auto expected_count = parse_count(argv[2]);
        const auto start = Clock::now();
        const auto index = search::Index::load(argv[1]);
        require(index.document_count() == expected_count,
                "document count mismatch: expected " + std::to_string(expected_count) + ", got " +
                    std::to_string(index.document_count()));
        const auto samples = prepare_samples(index);
        Head base;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto head = query_head(index, queries[i]);
            for (const auto id : head.ids) {
                const auto document = index.document(id);
                const auto tokens = search::tokenize(document.text);
                const Words words(tokens.begin(), tokens.end());
                require(expected_match(i, words),
                        "head result disagrees with stored text at DocId " + std::to_string(id) +
                            " for query: " + std::string(queries[i]));
            }
            check_samples(index, queries[i], samples,
                          [i](const Sample& sample) { return sample.expected[i]; });
            print_head(queries[i], head);
            if (i == 0)
                base = head;
        }
        check_unknown(index, samples, base);
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        std::cout << "PASS documents=" << index.document_count() << " terms=" << index.term_count()
                  << " postings=" << index.posting_count()
                  << " sampled_documents=" << samples.size()
                  << " checked_queries=" << queries.size() + 4 << " elapsed_seconds=" << std::fixed
                  << std::setprecision(3) << seconds << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
