#include "import/json_record.hpp"

#include "search/text.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace search::detail {
namespace {

class RecordParser {
public:
    explicit RecordParser(std::string_view input, std::string* url) : input_(input), url_(url) {
        if (url_)
            url_->clear();
    }

    Document parse() {
        validate_utf8(input_);
        whitespace();
        expect('{');
        container(0, '}');
        whitespace();
        if (position_ != input_.size())
            error("trailing data after JSON object");
        if (required_fields_ != 4) {
            error("required string fields: docid, title, headings, body");
        }
        if (docid_.empty())
            error("docid must not be empty");

        std::string text;
        text.reserve(title_.size() + headings_.size() + body_.size() + 2);
        append_section(text, title_);
        append_section(text, headings_);
        append_section(text, body_);
        return {std::move(docid_), std::move(title_), std::move(text)};
    }

private:
    static constexpr unsigned max_depth = 64;

    [[noreturn]] void error(const std::string& message) const {
        throw std::runtime_error("JSON byte " + std::to_string(position_ + 1) + ": " + message);
    }

    static void append_section(std::string& text, const std::string& section) {
        if (section.empty())
            return;
        if (!text.empty() && text.back() != '\n' && section.front() != '\n')
            text += '\n';
        text += section;
    }

    void whitespace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                break;
            ++position_;
        }
    }

    bool take(char expected) {
        if (position_ == input_.size() || input_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    void expect(char expected) {
        if (!take(expected))
            error(std::string("expected '") + expected + "'");
    }

    static bool digit(char ch) { return ch >= '0' && ch <= '9'; }

    std::uint32_t hex4() {
        if (input_.size() - position_ < 4)
            error("incomplete Unicode escape");
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char ch = input_[position_++];
            result <<= 4;
            if (ch >= '0' && ch <= '9')
                result |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f')
                result |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F')
                result |= static_cast<unsigned>(ch - 'A' + 10);
            else
                error("invalid hexadecimal digit in Unicode escape");
        }
        return result;
    }

    static void utf8(std::string& result, std::uint32_t cp) {
        if (cp <= 0x7f)
            result += static_cast<char>(cp);
        else if (cp <= 0x7ff) {
            result += static_cast<char>(0xc0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp <= 0xffff) {
            result += static_cast<char>(0xe0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            result += static_cast<char>(0xf0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    std::string string(bool retain = true) {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const auto ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"')
                return result;
            if (ch < 0x20)
                error("unescaped control byte in string");
            if (ch != '\\') {
                if (retain)
                    result += static_cast<char>(ch);
                continue;
            }
            if (position_ == input_.size())
                error("incomplete string escape");
            const char escape = input_[position_++];
            if (escape == 'u') {
                auto cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (!take('\\') || !take('u'))
                        error("high surrogate requires a low surrogate");
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff)
                        error("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    error("unpaired low surrogate");
                if (retain)
                    utf8(result, cp);
                continue;
            }
            const auto index = std::string_view("\"\\/bfnrt").find(escape);
            if (index == std::string_view::npos)
                error("invalid string escape");
            if (retain)
                result += "\"\\/\b\f\n\r\t"[index];
        }
        error("unterminated string");
    }

    bool digits() {
        const auto start = position_;
        while (position_ < input_.size() && digit(input_[position_]))
            ++position_;
        return position_ != start;
    }

    void number() {
        take('-');
        if (position_ == input_.size())
            error("incomplete number");
        if (take('0')) {
            if (position_ < input_.size() && digit(input_[position_]))
                error("leading zero in number");
        } else if (!digits())
            error("invalid number");
        if (take('.') && !digits())
            error("missing fraction digits");
        if (take('e') || take('E')) {
            if (!take('+'))
                take('-');
            if (!digits())
                error("missing exponent digits");
        }
    }

    void literal(std::string_view word) {
        if (input_.substr(position_, word.size()) != word)
            error("invalid JSON value");
        position_ += word.size();
    }

    void value(unsigned depth) {
        whitespace();
        if (position_ == input_.size())
            error("missing JSON value");
        const char ch = input_[position_];
        if (ch == '"') {
            string(false);
            return;
        }
        if (ch == 't') {
            literal("true");
            return;
        }
        if (ch == 'f') {
            literal("false");
            return;
        }
        if (ch == 'n') {
            literal("null");
            return;
        }
        if (ch == '-' || digit(ch)) {
            number();
            return;
        }
        if (ch != '{' && ch != '[')
            error("invalid JSON value");
        if (depth == max_depth)
            error("JSON nesting exceeds 64 containers");
        ++position_;
        container(depth, ch == '{' ? '}' : ']');
    }

    void container(unsigned depth, char end) {
        std::unordered_set<std::string> keys;
        whitespace();
        if (take(end))
            return;
        for (;;) {
            whitespace();
            std::string key;
            if (end == '}') {
                key = string();
                if (!keys.insert(key).second)
                    error("duplicate object key: " + key);
                whitespace();
                expect(':');
                whitespace();
            }
            if (depth == 0 && key == "docid") {
                docid_ = string();
                ++required_fields_;
            } else if (depth == 0 && key == "title") {
                title_ = string();
                ++required_fields_;
            } else if (depth == 0 && key == "headings") {
                headings_ = string();
                ++required_fields_;
            } else if (depth == 0 && key == "body") {
                body_ = string();
                ++required_fields_;
            } else if (depth == 0 && key == "url" && url_)
                *url_ = string();
            else
                value(depth + 1);
            whitespace();
            if (take(end))
                return;
            expect(',');
        }
    }

    std::string_view input_;
    std::string* url_;
    std::size_t position_ = 0;
    std::string docid_, title_, headings_, body_;
    unsigned required_fields_ = 0;
};

}

Document parse_msmarco_record(std::string_view input, std::string* url) {
    return RecordParser(input, url).parse();
}

}
