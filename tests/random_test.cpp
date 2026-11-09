#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include "search/query.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace search_tests {

namespace {

enum class NodeKind { term, conjunction, disjunction, negation };

struct BooleanNode {
    NodeKind kind = NodeKind::term;
    std::string term;
    std::unique_ptr<BooleanNode> left;
    std::unique_ptr<BooleanNode> right;
};

const std::vector<std::string> vocabulary{"alpha", "beta",  "gamma", "delta", "epsilon", "еж",
                                          "якорь", "omega", "and",   "or",    "not"};

std::unique_ptr<BooleanNode> random_tree(std::mt19937& random, unsigned depth) {
    auto node = std::make_unique<BooleanNode>();
    const auto choice = random() % 5;
    if (depth == 0 || choice == 0) {
        const auto term = random() % (vocabulary.size() + 2);
        node->term = term < vocabulary.size() ? vocabulary[term] : "unknown";
        return node;
    }
    node->kind = choice == 1   ? NodeKind::negation
                 : choice == 2 ? NodeKind::disjunction
                               : NodeKind::conjunction;
    node->left = random_tree(random, depth - 1);
    if (node->kind != NodeKind::negation)
        node->right = random_tree(random, depth - 1);
    return node;
}

bool evaluate(const BooleanNode& node, const std::set<std::string>& document) {
    switch (node.kind) {
    case NodeKind::term:
        return document.count(node.term) != 0;
    case NodeKind::conjunction:
        return evaluate(*node.left, document) && evaluate(*node.right, document);
    case NodeKind::disjunction:
        return evaluate(*node.left, document) || evaluate(*node.right, document);
    case NodeKind::negation:
        return !evaluate(*node.left, document);
    }
    throw std::runtime_error("unreachable oracle node");
}

std::string render_query(const BooleanNode& node, std::mt19937& random) {
    if (node.kind == NodeKind::term)
        return node.term;
    if (node.kind == NodeKind::negation) {
        return "NOT (" + render_query(*node.left, random) + ")";
    }
    const auto left = render_query(*node.left, random);
    const auto right = render_query(*node.right, random);
    const auto operation = node.kind == NodeKind::disjunction ? " OR "
                           : random() % 2                     ? " AND "
                                                              : " ";
    return "(" + left + operation + right + ")";
}

void check_random_advances(const Index& index, const std::string& query,
                           const std::vector<DocId>& expected, std::mt19937& random) {
    const std::vector<DocId> first_targets{0,
                                           1,
                                           static_cast<DocId>(index.document_count() / 2),
                                           static_cast<DocId>(index.document_count()),
                                           static_cast<DocId>(index.document_count() + 2),
                                           search::end_doc};
    for (const auto target : first_targets) {
        auto fresh = search::make_query(index, query);
        auto match = std::lower_bound(expected.begin(), expected.end(), target);
        const auto wanted = match == expected.end() ? search::end_doc : *match;
        SEARCH_TEST_CHECK(fresh->advance(target) == wanted);
        SEARCH_TEST_CHECK(fresh->advance(0) == wanted);
        if (match != expected.end())
            ++match;
        SEARCH_TEST_CHECK(fresh->next() == (match == expected.end() ? search::end_doc : *match));
    }
    auto cursor = search::make_query(index, query);
    std::size_t position = 0;
    const auto expected_doc = [&] {
        return position < expected.size() ? expected[position] : search::end_doc;
    };
    SEARCH_TEST_CHECK(cursor->doc() == expected_doc());
    for (unsigned step = 0; step < 35; ++step) {
        if (random() % 3 == 0) {
            if (position < expected.size())
                ++position;
            SEARCH_TEST_CHECK(cursor->next() == expected_doc());
        } else {
            const auto target = static_cast<DocId>(random() % (index.document_count() + 5));
            position = static_cast<std::size_t>(
                std::lower_bound(expected.begin() + position, expected.end(), target) -
                expected.begin());
            SEARCH_TEST_CHECK(cursor->advance(target) == expected_doc());
        }
        SEARCH_TEST_CHECK(cursor->doc() == expected_doc());
    }
    SEARCH_TEST_CHECK(cursor->advance(search::end_doc) == search::end_doc);
    SEARCH_TEST_CHECK(cursor->advance(0) == search::end_doc);
    SEARCH_TEST_CHECK(cursor->next() == search::end_doc);
}

}

void test_random_boolean_oracle() {
    constexpr unsigned seed = 0x5eeda11u;
    std::mt19937 random(seed);
    const std::vector<std::string> separators{" ", "\n", ", ", "-", " / "};
    std::size_t checked_queries = 0;
    for (unsigned corpus_number = 0; corpus_number < 40; ++corpus_number) {
        const auto document_count = static_cast<std::size_t>(random() % 25);
        std::vector<Document> documents;
        std::vector<std::set<std::string>> word_sets;
        for (std::size_t id = 0; id < document_count; ++id) {
            std::set<std::string> words;
            std::string text;
            for (const auto& word : vocabulary) {
                if (random() % 3 == 0)
                    continue;
                words.insert(word);
                const auto repetitions = 1 + random() % 4;
                for (unsigned repetition = 0; repetition < repetitions; ++repetition) {
                    text += separators[random() % separators.size()];
                    text += word == "еж" && random() % 2 ? "ЁЖ" : word;
                }
            }
            documents.push_back({"document-" + std::to_string(id) + ".txt", "", text});
            word_sets.push_back(std::move(words));
        }
        const auto index = make_index(std::move(documents));
        check_index_invariants(index);
        for (unsigned query_number = 0; query_number < 80; ++query_number) {
            const auto tree = random_tree(random, 4);
            const auto query = render_query(*tree, random);
            std::vector<DocId> expected;
            for (std::size_t id = 0; id < word_sets.size(); ++id) {
                if (evaluate(*tree, word_sets[id]))
                    expected.push_back(static_cast<DocId>(id));
            }
            check_results(index, query, expected);
            check_random_advances(index, query, expected, random);
            const auto limit = static_cast<std::size_t>(random() % (document_count + 3));
            auto limited = search::make_query(index, query);
            const std::vector<DocId> prefix(expected.begin(),
                                            expected.begin() + std::min(limit, expected.size()));
            SEARCH_TEST_CHECK(collect(*limited, limit) == prefix);
            ++checked_queries;
        }
    }
    SEARCH_TEST_CHECK(checked_queries == 3200);
}

}
