#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace search::detail {

enum class Kind { Term, And, Or, Not, LeftParen, RightParen, End };

struct Node {
    Kind kind;
    std::string word;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

struct Token {
    Kind kind;
    std::string word;
};

std::vector<Token> lex(std::string_view query);

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<Node> parse();

private:
    Kind peek() const;
    static std::unique_ptr<Node> binary(Kind kind, std::unique_ptr<Node> left,
                                        std::unique_ptr<Node> right);
    std::unique_ptr<Node> parse_or(unsigned depth);
    std::unique_ptr<Node> parse_and(unsigned depth);
    std::unique_ptr<Node> parse_unary(unsigned depth);

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
};

}
