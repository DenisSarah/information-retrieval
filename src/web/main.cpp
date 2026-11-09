#include "web/http.hpp"
#include "web/urls.hpp"
#include "cli/output.hpp"
#include "search/index.hpp"
#include "search/query.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t number(std::string_view value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("Требуется целое неотрицательное число");
    }
    return result;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Не удалось открыть " + path.string());
    std::string result{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad())
        throw std::runtime_error("Не удалось прочитать " + path.string());
    return result;
}

std::map<std::string, std::string> parameters(std::string_view input) {
    std::map<std::string, std::string> result;
    while (!input.empty()) {
        const auto end = input.find('&');
        const auto item = input.substr(0, end);
        const auto equals = item.find('=');
        if (equals == std::string_view::npos)
            throw std::runtime_error("Некорректный параметр запроса");
        if (!result
                 .emplace(web::decode_query(item.substr(0, equals)),
                          web::decode_query(item.substr(equals + 1)))
                 .second) {
            throw std::runtime_error("Параметр запроса указан несколько раз");
        }
        if (end == std::string_view::npos)
            break;
        input.remove_prefix(end + 1);
    }
    return result;
}

web::Response search_results(const search::Index& index, std::string_view query,
                             std::size_t limit) {
    const auto started = std::chrono::steady_clock::now();
    auto cursor = search::make_query(index, query);
    std::vector<search::DocId> ids;
    std::uint64_t total = 0;
    while (cursor->doc() != search::end_doc) {
        if (ids.size() < limit)
            ids.push_back(cursor->doc());
        ++total;
        cursor->next();
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    std::string body =
        "{\"query\":" + web::json_string(query) +
        ",\"documents\":" + std::to_string(index.document_count()) +
        ",\"total\":" + std::to_string(total) + ",\"limit\":" + std::to_string(limit) +
        ",\"elapsed_ms\":" + std::to_string(elapsed) +
        ",\"has_more\":" + (total > ids.size() ? "true" : "false") + ",\"results\":[";
    for (const auto id : ids) {
        if (id != ids.front())
            body += ',';
        const auto document = index.document(id);
        body += "{\"id\":" + std::to_string(id) +
                ",\"external_id\":" + web::json_string(document.external_id) +
                ",\"title\":" + web::json_string(document.title) +
                ",\"snippet\":" + web::json_string(cli::snippet(document.text)) + '}';
    }
    return {200, "application/json; charset=utf-8", body + "]}", {}};
}

web::Response route(const search::Index& index, const std::filesystem::path& corpus,
                    const std::string& page, std::string_view target) {
    const auto separator = target.find('?');
    const auto path = target.substr(0, separator);
    if (path == "/")
        return {200, "text/html; charset=utf-8", page, {}};
    if (path == "/favicon.ico")
        return {204, "image/x-icon", {}, {}};
    if (path == "/api/search") {
        try {
            const auto values =
                parameters(separator == std::string_view::npos ? std::string_view{}
                                                               : target.substr(separator + 1));
            if (!values.count("q")) {
                throw std::runtime_error("Не указан поисковый запрос");
            }
            const auto limit = values.count("limit") ? number(values.at("limit")) : 10;
            if (limit == 0 || limit > 1000) {
                throw std::runtime_error("Число результатов должно быть от 1 до 1000");
            }
            return search_results(index, values.at("q"), static_cast<std::size_t>(limit));
        } catch (const std::exception& error) {
            return {400,
                    "application/json; charset=utf-8",
                    "{\"error\":" + web::json_string(error.what()) + '}',
                    {}};
        }
    }
    const bool open = path.substr(0, 6) == "/open/";
    if (open || path.substr(0, 10) == "/document/") {
        const auto id = number(path.substr(open ? 6 : 10));
        if (id >= index.document_count()) {
            return {404, "text/plain; charset=utf-8", "Документ не найден", {}};
        }
        const auto document = index.document(static_cast<search::DocId>(id));
        if (open)
            return {303, "text/plain; charset=utf-8", "Открываем исходную страницу…",
                    web::source_url(corpus, document.external_id)};
        return {200,
                "text/plain; charset=utf-8",
                document.title + "\n" + document.external_id + "\n\n" + document.text,
                {}};
    }
    return {404, "text/plain; charset=utf-8", "Страница не найдена", {}};
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc > 4)
            throw std::runtime_error("Использование: search-web [INDEX] [CORPUS] [PORT]");
        const auto index = search::Index::load(argc > 1 ? argv[1] : "build/msmarco.idx");
        const std::filesystem::path corpus = argc > 2 ? argv[2] : "data/msmarco/msmarco_v2.1_doc";
        const auto port = argc > 3 ? number(argv[3]) : 8000;
        if (port == 0 || port > 65535)
            throw std::runtime_error("PORT должен быть от 1 до 65535");
        const auto root = std::filesystem::absolute(argv[0]).parent_path().parent_path();
        const auto page = read_file(root / "web/index.html");
        std::cout << "Поиск: http://127.0.0.1:" << port
                  << "\nДокументов: " << index.document_count() << "\nОстановить: Ctrl+C\n"
                  << std::flush;
        web::serve(static_cast<std::uint16_t>(port),
                   [index, corpus, page](std::string_view target) {
                       return route(index, corpus, page, target);
                   });
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }
}
