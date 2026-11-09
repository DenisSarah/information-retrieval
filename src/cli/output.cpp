#include "cli/output.hpp"

#include "search/cursor.hpp"
#include "search/document.hpp"
#include "search/index.hpp"
#include "search/query.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

std::string safe_inline(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f) {
            result += "\\x";
            result += hex[ch >> 4];
            result += hex[ch & 15];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

std::string snippet(std::string_view text) {
    constexpr std::size_t max_characters = 180;
    std::string result;
    std::size_t characters = 0;
    bool pending_space = false;
    for (unsigned char ch : text) {
        if (ch <= 0x20 || ch == 0x7f) {
            pending_space = !result.empty();
            continue;
        }
        const bool starts_character = (ch & 0xc0) != 0x80;
        if (starts_character && characters == max_characters) {
            result += "…";
            break;
        }
        if (pending_space) {
            result += ' ';
            pending_space = false;
        }
        result += static_cast<char>(ch);
        if (starts_character)
            ++characters;
    }
    return result;
}

void help(std::ostream& output) {
    output << R"(Булев поиск по MS MARCO Document Corpus

Использование:
  search index CORPUS INDEX [MEMORY_MIB]  построить индекс JSONL/GZIP, сегмент 256 MiB по умолчанию
  search search INDEX QUERY [LIMIT]  выполнить запрос (LIMIT > 0, по умолчанию 10)
  search shell INDEX              запросы до EOF или :quit
  search show INDEX DOC_ID        показать сохранённый текст документа
  search stats INDEX              статистика индекса
  search verify INDEX             полная последовательная проверка структуры
  search help                     эта справка

QUERY передаётся одним аргументом оболочки, например:
  search search build/msmarco.idx 'neural AND (search OR retrieval)'

Операторы: AND, OR, NOT (заглавными буквами), скобки.
Пробел между термами означает AND. Приоритет: NOT, AND, OR.
Нормализация: нижний регистр, ё → е; без стемминга и морфологии.
Выдача отсортирована по docID, ранжирования по релевантности нет.
docID начинается с 0: порядок файлов и строк MS MARCO; внешние ID сохранены.
Внешний TAR предварительно распаковывается; внутренние .gz распаковывать не нужно.
)";
}

void print_results(const search::Index& index, std::string_view query, std::size_t limit) {
    const auto started = std::chrono::steady_clock::now();
    auto cursor = search::make_query(index, query);
    std::vector<search::DocId> matches;
    matches.reserve(std::min(limit, static_cast<std::size_t>(index.document_count())));
    while (cursor->doc() != search::end_doc && matches.size() < limit) {
        matches.push_back(cursor->doc());
        cursor->next();
    }
    const bool has_more = cursor->doc() != search::end_doc;
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();

    std::cout << "Запрос: " << safe_inline(query) << '\n';
    for (const auto id : matches) {
        const auto& document = index.document(id);
        std::cout << '[' << id << "] " << safe_inline(document.title) << '\n'
                  << "  ID: " << safe_inline(document.external_id) << '\n'
                  << "  " << snippet(document.text) << '\n';
    }
    std::cout << "Показано: " << matches.size();
    if (has_more)
        std::cout << " (есть ещё совпадения; увеличьте LIMIT)";
    std::cout << ". Время запроса: " << std::fixed << std::setprecision(3) << elapsed
              << " мс (без загрузки индекса и печати).\n";
}

void show_document(const search::Index& index, std::uint64_t id) {
    if (id >= index.document_count()) {
        throw std::runtime_error("DOC_ID вне диапазона документов индекса");
    }
    const auto& document = index.document(static_cast<std::size_t>(id));
    std::cout << '[' << id << "] " << safe_inline(document.title) << '\n'
              << "ID: " << safe_inline(document.external_id) << "\n\n";
    for (char ch : document.text) {
        if (ch == '\n' || ch == '\t')
            std::cout.put(ch);
        else
            std::cout << safe_inline(std::string_view(&ch, 1));
    }
    if (document.text.empty() || document.text.back() != '\n')
        std::cout << '\n';
}

}
