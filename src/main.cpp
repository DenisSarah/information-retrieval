#include "cli/output.hpp"
#include "search/index.hpp"
#include "index/builder.hpp"
#include "import/msmarco.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::size_t default_limit = 10;

std::uint64_t number(std::string_view value, std::string_view name) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error(std::string(name) + ": требуется целое неотрицательное число");
    }
    return result;
}

void require_arguments(int argc, int expected, std::string_view command) {
    if (argc != expected) {
        throw std::runtime_error("Неверное число аргументов команды " + std::string(command) +
                                 ". См. search help");
    }
}

}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            cli::help(std::cerr);
            return 1;
        }
        const std::string_view command = argv[1];
        if (command == "help") {
            require_arguments(argc, 2, command);
            cli::help(std::cout);
        } else if (command == "index") {
            if (argc != 4 && argc != 5) {
                throw std::runtime_error("Использование: search index CORPUS INDEX [MEMORY_MIB]");
            }
            search::BuildOptions options;
            if (argc == 5) {
                const auto mib = number(argv[4], "MEMORY_MIB");
                if (!mib || mib > std::numeric_limits<std::size_t>::max() / (1024 * 1024)) {
                    throw std::runtime_error("MEMORY_MIB вне допустимого диапазона");
                }
                options.memory_bytes = static_cast<std::size_t>(mib) * 1024 * 1024;
            }
            const auto started = std::chrono::steady_clock::now();
            search::IndexBuilder builder(argv[3], options);
            std::uint64_t documents = 0;
            search::detail::read_msmarco(argv[2], [&](search::Document document) {
                builder.add(std::move(document));
                if (++documents % 100000 == 0) {
                    std::cerr << "Прочитано документов: " << documents << '\n';
                }
            });
            std::cerr << "Слияние сегментов...\n";
            const auto stats = builder.finish();
            const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::cout << "Индекс сохранён: " << cli::safe_inline(argv[3]) << '\n'
                      << "Документов: " << stats.documents << "; термов: " << stats.terms
                      << "; записей posting lists: " << stats.postings << '\n'
                      << "Байт сохранённого текста: " << stats.text_bytes << '\n'
                      << "Построение и сохранение: " << std::fixed << std::setprecision(3)
                      << elapsed << " с.\n";
        } else if (command == "search") {
            if (argc != 4 && argc != 5) {
                throw std::runtime_error(
                    "Неверное число аргументов search. Передайте QUERY одним аргументом в кавычках");
            }
            std::size_t limit = default_limit;
            if (argc == 5) {
                const auto parsed = number(argv[4], "LIMIT");
                if (parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
                    throw std::runtime_error(
                        "LIMIT должен быть положительным числом в диапазоне size_t");
                }
                limit = static_cast<std::size_t>(parsed);
            }
            const auto index = search::Index::load(argv[2]);
            cli::print_results(index, argv[3], limit);
        } else if (command == "shell") {
            require_arguments(argc, 3, command);
            const auto index = search::Index::load(argv[2]);
            std::cout
                << "Индекс загружен. Вводите запросы; :quit завершает работу, :help показывает справку.\n";
            std::string query;
            while (std::cout << "поиск> " << std::flush, std::getline(std::cin, query)) {
                if (query == ":quit")
                    break;
                if (query == ":help") {
                    cli::help(std::cout);
                    continue;
                }
                if (query.empty())
                    continue;
                try {
                    cli::print_results(index, query, default_limit);
                } catch (const std::exception& error) {
                    std::cerr << "Ошибка запроса: " << cli::safe_inline(error.what()) << '\n';
                }
            }
            if (std::cin.bad())
                throw std::runtime_error("Ошибка чтения запроса из stdin");
        } else if (command == "show") {
            require_arguments(argc, 4, command);
            const auto id = number(argv[3], "DOC_ID");
            const auto index = search::Index::load(argv[2]);
            cli::show_document(index, id);
        } else if (command == "verify") {
            require_arguments(argc, 3, command);
            const auto index = search::Index::load(argv[2]);
            index.verify();
            std::cout << "Структура индекса проверена. Документов: " << index.document_count()
                      << '\n';
        } else if (command == "stats") {
            require_arguments(argc, 3, command);
            const auto index = search::Index::load(argv[2]);
            std::cout << "Документов: " << index.document_count() << '\n'
                      << "Термов: " << index.term_count() << '\n'
                      << "Записей posting lists: " << index.posting_count() << '\n'
                      << "Байт сохранённого текста: " << index.text_bytes() << '\n'
                      << "Размер файла индекса: " << std::filesystem::file_size(argv[2])
                      << " байт\n";
        } else {
            throw std::runtime_error("Неизвестная команда: " + std::string(command) +
                                     ". См. search help");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << cli::safe_inline(error.what()) << '\n';
        return 1;
    }
}
