#include "query/parser.hpp"

#include "search/text.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace search::detail {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}

std::vector<Token> lex(std::string_view query) {
    if (query.size() > 8192) {
        throw std::runtime_error("Запрос превышает ограничение 8192 байта");
    }
    for (const std::string_view quote :
         {"\"", "'", u8"«", u8"»", u8"“", u8"”", u8"‘", u8"’", u8"„", u8"‚"}) {
        if (query.find(quote) != std::string_view::npos) {
            throw std::runtime_error("Кавычки и фразовый поиск не поддерживаются");
        }
    }

    std::vector<Token> tokens;
    const auto append = [&tokens](Kind kind, std::string word = {}) {
        if (tokens.size() == 256) {
            throw std::runtime_error("Запрос превышает ограничение 256 токенов");
        }
        tokens.push_back({kind, std::move(word)});
    };

    std::size_t position = 0;
    while (position < query.size()) {
        const char c = query[position];
        if (is_space(c)) {
            ++position;
        } else if (c == '(' || c == ')') {
            append(c == '(' ? Kind::LeftParen : Kind::RightParen);
            ++position;
        } else {
            const auto begin = position;
            while (position < query.size() && !is_space(query[position]) &&
                   query[position] != '(' && query[position] != ')') {
                ++position;
            }
            const auto chunk = query.substr(begin, position - begin);
            if (chunk == "AND") {
                append(Kind::And);
            } else if (chunk == "OR") {
                append(Kind::Or);
            } else if (chunk == "NOT") {
                append(Kind::Not);
            } else {
                auto words = tokenize(chunk);
                if (words.empty()) {
                    throw std::runtime_error("Фрагмент запроса не содержит поисковых термов");
                }
                for (auto& word : words) {
                    append(Kind::Term, std::move(word));
                }
            }
        }
    }
    if (tokens.empty()) {
        throw std::runtime_error("Пустой поисковый запрос");
    }
    return tokens;
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
}

std::unique_ptr<Node> Parser::parse() {
    auto node = parse_or(0);
    if (peek() != Kind::End) {
        throw std::runtime_error("Неожиданный токен после выражения запроса");
    }
    return node;
}

Kind Parser::peek() const {
    return position_ == tokens_.size() ? Kind::End : tokens_[position_].kind;
}

std::unique_ptr<Node> Parser::binary(Kind kind, std::unique_ptr<Node> left,
                                     std::unique_ptr<Node> right) {
    return std::make_unique<Node>(Node{kind, {}, std::move(left), std::move(right)});
}

std::unique_ptr<Node> Parser::parse_or(unsigned depth) {
    auto node = parse_and(depth);
    while (peek() == Kind::Or) {
        ++position_;
        node = binary(Kind::Or, std::move(node), parse_and(depth));
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_and(unsigned depth) {
    auto node = parse_unary(depth);
    while (peek() == Kind::And || peek() == Kind::Term || peek() == Kind::LeftParen ||
           peek() == Kind::Not) {
        if (peek() == Kind::And)
            ++position_;
        node = binary(Kind::And, std::move(node), parse_unary(depth));
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_unary(unsigned depth) {
    const auto kind = peek();
    if (kind == Kind::Not || kind == Kind::LeftParen) {
        if (depth == 64) {
            throw std::runtime_error("Вложенность запроса превышает 64 уровня");
        }
        ++position_;
        if (kind == Kind::Not) {
            auto child = parse_unary(depth + 1);
            if (child->kind == Kind::Not) {
                return std::move(child->left);
            }
            return std::make_unique<Node>(Node{Kind::Not, {}, std::move(child), {}});
        }
        auto node = parse_or(depth + 1);
        if (peek() != Kind::RightParen) {
            throw std::runtime_error("В запросе ожидается закрывающая скобка ')'");
        }
        ++position_;
        return node;
    }
    if (kind != Kind::Term) {
        throw std::runtime_error("В запросе ожидается терм, NOT или '('");
    }
    return std::make_unique<Node>(Node{Kind::Term, std::move(tokens_[position_++].word), {}, {}});
}

}
