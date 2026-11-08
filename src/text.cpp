#include "search/text.hpp"

#include <cstdint>

#include <stdexcept>
#include <utility>

namespace search {
namespace {

template <typename Visitor> void codepoints(std::string_view text, Visitor visit) {
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t cp = first;
        unsigned extra = 0;
        std::uint32_t minimum = 0;
        if (first < 0x80) {
        } else if (first >= 0xc2 && first <= 0xdf) {
            cp = first & 0x1f;
            extra = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            cp = first & 0x0f;
            extra = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            cp = first & 0x07;
            extra = 3;
            minimum = 0x10000;
        } else {
            throw std::runtime_error("Некорректный UTF-8, байт " + std::to_string(start));
        }
        bool valid = extra <= text.size() - i;
        for (unsigned j = 0; valid && j < extra; ++j) {
            const auto next = static_cast<unsigned char>(text[i++]);
            valid = (next & 0xc0) == 0x80;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (!valid || cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            throw std::runtime_error("Некорректный UTF-8, байт " + std::to_string(start));
        }
        visit(cp);
    }
}

}

void validate_utf8(std::string_view text) {
    codepoints(text, [](std::uint32_t) {});
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    };
    codepoints(text, [&](std::uint32_t cp) {
        if (cp >= 'A' && cp <= 'Z')
            cp += 'a' - 'A';
        if (cp >= 0x410 && cp <= 0x42f)
            cp += 0x20;
        if (cp == 0x401 || cp == 0x451)
            cp = 0x435;
        if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
            word.push_back(static_cast<char>(cp));
        } else if (cp >= 0x430 && cp <= 0x44f) {
            word.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            word.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            flush();
        }
    });
    flush();
    return words;
}

}
