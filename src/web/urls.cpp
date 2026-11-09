#include "web/urls.hpp"

#include "import/json_record.hpp"

#include <zlib.h>

#include <array>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string>

namespace web {
namespace {

bool digit(char ch) {
    return ch >= '0' && ch <= '9';
}

std::string read_record(gzFile file) {
    constexpr std::size_t max_bytes = 64 * 1024 * 1024;
    std::array<char, 64 * 1024> buffer{};
    std::string record;
    for (;;) {
        const int count = gzread(file, buffer.data(), static_cast<unsigned>(buffer.size()));
        if (count < 0) {
            int code = Z_OK;
            const char* message = gzerror(file, &code);
            throw std::runtime_error("Ошибка чтения gzip: " +
                                     std::string(message ? message : "неизвестная ошибка"));
        }
        if (count == 0) {
            if (record.empty())
                throw std::runtime_error("Документ отсутствует по указанному смещению");
            return record;
        }
        const std::string_view chunk(buffer.data(), static_cast<std::size_t>(count));
        const auto line = chunk.substr(0, chunk.find('\n'));
        if (line.size() > max_bytes - record.size())
            throw std::runtime_error("Документ MS MARCO превышает 64 МиБ");
        record += line;
        if (line.size() < chunk.size())
            return record;
    }
}

void validate_url(std::string_view url) {
    const std::size_t start = url.substr(0, 7) == "http://"    ? 7
                              : url.substr(0, 8) == "https://" ? 8
                                                               : 0;
    if (start == 0)
        throw std::runtime_error("У документа нет допустимой HTTP/HTTPS-ссылки");
    for (unsigned char ch : url) {
        if (ch <= 0x20 || ch == 0x7f || ch == '\\')
            throw std::runtime_error("Недопустимые символы в URL документа");
    }
    url.remove_prefix(start);
    auto authority = url.substr(0, url.find_first_of("/?#"));
    const auto at = authority.rfind('@');
    if (at != std::string_view::npos)
        authority.remove_prefix(at + 1);
    if (authority.empty())
        throw std::runtime_error("В URL документа отсутствует адрес сайта");
    if (authority.front() == '[') {
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos || bracket == 1 ||
            (bracket + 1 != authority.size() && authority[bracket + 1] != ':')) {
            throw std::runtime_error("Некорректный адрес сайта в URL документа");
        }
    } else if (authority.front() == ':') {
        throw std::runtime_error("В URL документа отсутствует адрес сайта");
    }
}

}

std::string source_url(const std::filesystem::path& corpus, std::string_view external_id) {
    constexpr std::string_view prefix = "msmarco_v2.1_doc_";
    if (external_id.substr(0, prefix.size()) != prefix)
        throw std::runtime_error("Документ не содержит ID MS MARCO V2.1");
    const auto suffix = external_id.substr(prefix.size());
    if (suffix.size() < 4 || !digit(suffix[0]) || !digit(suffix[1]) || suffix[2] != '_') {
        throw std::runtime_error("Некорректный ID MS MARCO V2.1");
    }
    z_off_t offset = 0;
    const auto end = suffix.data() + suffix.size();
    const auto parsed = std::from_chars(suffix.data() + 3, end, offset);
    if (!digit(suffix[3]) || parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::runtime_error("Некорректное смещение в ID MS MARCO");
    }
    const auto path = corpus / (std::string(external_id.substr(0, prefix.size() + 2)) + ".json.gz");
    std::unique_ptr<gzFile_s, decltype(&gzclose)> file(gzopen(path.c_str(), "rb"), gzclose);
    if (!file)
        throw std::runtime_error("Не удалось открыть исходный файл MS MARCO: " + path.string());
    if (gzseek(file.get(), offset, SEEK_SET) != offset)
        throw std::runtime_error("Не удалось перейти к документу в gzip");
    std::string url;
    const auto document = search::detail::parse_msmarco_record(read_record(file.get()), &url);
    if (document.external_id != external_id)
        throw std::runtime_error("ID исходного документа не совпал с индексом");
    validate_url(url);
    return url;
}

}
