#include "search/query.hpp"

#include "query/cursors.hpp"
#include "query/parser.hpp"
#include "search/index.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace search::detail {
namespace {

std::unique_ptr<Cursor> compile(const Index& index, const Node& node);

void compile_conjunction(const Index& index, const Node& node, std::unique_ptr<Cursor>& positive,
                         std::unique_ptr<Cursor>& negative) {
    if (node.kind == Kind::And) {
        compile_conjunction(index, *node.left, positive, negative);
        compile_conjunction(index, *node.right, positive, negative);
    } else {
        const bool negated = node.kind == Kind::Not;
        auto& chain = negated ? negative : positive;
        auto cursor = compile(index, negated ? *node.left : node);
        if (chain) {
            cursor = std::make_unique<BinaryCursor>(negated ? Operation::Or : Operation::And,
                                                    std::move(chain), std::move(cursor));
        }
        chain = std::move(cursor);
    }
}

std::unique_ptr<Cursor> compile(const Index& index, const Node& node) {
    if (node.kind == Kind::Term) {
        return std::make_unique<PostingCursor>(index, index.find(node.word));
    }
    if (node.kind == Kind::Or) {
        return std::make_unique<BinaryCursor>(Operation::Or, compile(index, *node.left),
                                              compile(index, *node.right));
    }
    if (node.kind == Kind::Not) {
        return std::make_unique<NotCursor>(compile(index, *node.left), index.document_count());
    }

    std::unique_ptr<Cursor> positive;
    std::unique_ptr<Cursor> negative;
    compile_conjunction(index, node, positive, negative);
    if (!positive) {
        return std::make_unique<NotCursor>(std::move(negative), index.document_count());
    }
    if (!negative) {
        return positive;
    }
    return std::make_unique<BinaryCursor>(Operation::Difference, std::move(positive),
                                          std::move(negative));
}

}
}

namespace search {

std::unique_ptr<Cursor> make_query(const Index& index, std::string_view query) {
    if (index.document_count() > end_doc) {
        throw std::runtime_error("Число документов превышает пространство docID");
    }
    const auto tree = detail::Parser(detail::lex(query)).parse();
    return detail::compile(index, *tree);
}

}
