#include "index/builder_runs.hpp"

#include "index/binary_io.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace search::detail {
namespace {

std::string run_string(BinaryReader& reader) {
    const auto length = reader.number(4);
    if (!length || length > builder_record_limit || length > reader.remaining()) {
        throw std::runtime_error("Повреждена или слишком велика запись временного индекса");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    reader.bytes(result.data(), result.size());
    return result;
}

void write_postings(std::ostream& stream, const std::vector<DocId>& ids) {
    std::array<char, 4096> buffer;
    for (std::size_t begin = 0; begin < ids.size();) {
        const auto count = std::min(ids.size() - begin, buffer.size() / 4);
        for (std::size_t i = 0; i < count; ++i) {
            for (unsigned byte = 0; byte < 4; ++byte) {
                buffer[i * 4 + byte] = static_cast<char>((ids[begin + i] >> (8 * byte)) & 255);
            }
        }
        stream.write(buffer.data(), static_cast<std::streamsize>(count * 4));
        builder_check(stream);
        begin += count;
    }
}

class RunInput {
public:
    explicit RunInput(const std::filesystem::path& file) : reader_(file) { next_record(); }
    void next_record() {
        if (unread_)
            throw std::runtime_error("Не дочитан список временного индекса");
        word = reader_.remaining() ? run_string(reader_) : std::string{};
        if (word.empty())
            return;
        count = static_cast<std::uint32_t>(reader_.number(4));
        if (count > reader_.remaining() / 4)
            throw std::runtime_error("Поврежден список временного индекса");
        unread_ = count;
    }
    void copy_postings(std::ostream& output, DocId& previous, std::uint64_t documents = end_doc) {
        std::array<char, 4096> buffer;
        while (unread_) {
            const auto bytes =
                static_cast<std::size_t>(std::min<std::uint64_t>(unread_, buffer.size() / 4) * 4);
            reader_.bytes(buffer.data(), bytes);
            for (std::size_t offset = 0; offset < bytes; offset += 4) {
                DocId id = 0;
                for (unsigned byte = 0; byte < 4; ++byte) {
                    id |= DocId(static_cast<unsigned char>(buffer[offset + byte])) << (8 * byte);
                }
                if (id == end_doc || id >= documents || (previous != end_doc && id <= previous)) {
                    throw std::runtime_error("Нарушены границы или порядок docID в сегменте");
                }
                previous = id;
            }
            output.write(buffer.data(), static_cast<std::streamsize>(bytes));
            builder_check(output);
            unread_ -= bytes / 4;
        }
    }
    std::string word;
    std::uint32_t count = 0;

private:
    BinaryReader reader_;
    std::uint64_t unread_ = 0;
};

}

std::ofstream builder_output(const std::filesystem::path& file) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    builder_check(stream);
    return stream;
}

std::uint64_t builder_add(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        throw std::runtime_error("Размер индекса переполняет 64-битное смещение");
    }
    return a + b;
}

std::uint64_t builder_multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b) {
        throw std::runtime_error("Размер индекса переполняет 64-битное смещение");
    }
    return a * b;
}

void builder_check(const std::ios& stream) {
    if (!stream)
        throw std::runtime_error("Ошибка чтения или записи временного индекса");
}

void builder_close(std::ofstream& stream) {
    stream.close();
    builder_check(stream);
}

BuilderRun write_builder_run(const std::filesystem::path& file, const BuilderTerms& terms,
                             std::vector<std::string>& ids) {
    std::sort(ids.begin(), ids.end());
    auto stream = builder_output(file);
    BuilderRun run{file, 0};
    for (const auto& [word, ids] : terms) {
        write_string(stream, word);
        write_number(stream, checked_size32(ids.size()), 4);
        write_postings(stream, ids);
        run.max_record = std::max<std::uint64_t>(run.max_record, word.size());
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i && ids[i] == ids[i - 1])
            throw std::runtime_error("Повторный внешний ID документа: " + ids[i]);
        write_string(stream, ids[i]);
        write_number(stream, 0, 4);
        run.max_record = std::max<std::uint64_t>(run.max_record, ids[i].size());
    }
    builder_close(stream);
    return run;
}

BuilderRun merge_builder_runs(const std::filesystem::path& file,
                              const std::vector<BuilderRun>& runs) {
    std::vector<RunInput> readers;
    readers.reserve(runs.size());
    BuilderRun result{file, 0};
    for (const auto& run : runs) {
        readers.emplace_back(run.path);
        result.max_record = std::max(result.max_record, run.max_record);
    }
    const auto later = [&readers](std::size_t a, std::size_t b) {
        return std::tuple(readers[a].count == 0, std::string_view(readers[a].word), a) >
               std::tuple(readers[b].count == 0, std::string_view(readers[b].word), b);
    };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(later)> heap(later);
    for (std::size_t i = 0; i < readers.size(); ++i)
        if (!readers[i].word.empty())
            heap.push(i);
    auto stream = builder_output(file);
    std::vector<std::size_t> group;
    group.reserve(readers.size());
    while (!heap.empty()) {
        const std::string word = readers[heap.top()].word;
        const bool ids = readers[heap.top()].count == 0;
        group.clear();
        std::uint64_t count = 0;
        while (!heap.empty() && readers[heap.top()].word == word &&
               (readers[heap.top()].count == 0) == ids) {
            group.push_back(heap.top());
            count = builder_add(count, readers[heap.top()].count);
            heap.pop();
        }
        if (ids && group.size() > 1)
            throw std::runtime_error("Повторный внешний ID документа: " + word);
        write_string(stream, word);
        write_number(stream, checked_size32(count), 4);
        DocId previous = end_doc;
        for (const auto i : group) {
            auto& reader = readers[i];
            reader.copy_postings(stream, previous);
            reader.next_record();
            if (!reader.word.empty())
                heap.push(i);
        }
    }
    builder_close(stream);
    return result;
}

BuilderTermStats materialize_builder_terms(const BuilderRun* run, std::uint64_t documents,
                                           const std::filesystem::path& offsets,
                                           const std::filesystem::path& records,
                                           const std::filesystem::path& postings_file) {
    auto offset_stream = builder_output(offsets);
    auto record_stream = builder_output(records);
    auto posting_stream = builder_output(postings_file);
    BuilderTermStats stats;
    write_number(offset_stream, 0, 8);
    if (run) {
        RunInput reader(run->path);
        while (!reader.word.empty() && reader.count) {
            write_string(record_stream, reader.word);
            write_number(record_stream, stats.postings, 8);
            write_number(record_stream, reader.count, 4);
            stats.bytes = builder_add(stats.bytes, builder_add(16, reader.word.size()));
            stats.postings = builder_add(stats.postings, reader.count);
            stats.terms = builder_add(stats.terms, 1);
            write_number(offset_stream, stats.bytes, 8);
            DocId previous = end_doc;
            reader.copy_postings(posting_stream, previous, documents);
            reader.next_record();
        }
    }
    builder_close(offset_stream);
    builder_close(record_stream);
    builder_close(posting_stream);
    return stats;
}

}
