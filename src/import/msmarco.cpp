#include "import/msmarco.hpp"

#include "import/json_record.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace search::detail {
namespace {

namespace fs = std::filesystem;
constexpr std::size_t max_record_bytes = 64 * 1024 * 1024;
constexpr std::size_t buffer_bytes = 64 * 1024;

[[noreturn]] void file_error(const fs::path& file, std::size_t line, const std::string& message) {
    throw std::runtime_error(file.string() + ":" + std::to_string(line) + ": " + message);
}

class Lines {
public:
    Lines(const fs::path& file, const DocumentConsumer& consumer)
        : file_(file), consumer_(consumer) {}

    void feed(std::string_view bytes) {
        while (!bytes.empty()) {
            const auto count = std::min(bytes.find('\n'), bytes.size());
            if (count > max_record_bytes - record_.size())
                error("JSONL record exceeds 64 MiB");
            record_.append(bytes.data(), count);
            bytes.remove_prefix(count);
            if (!bytes.empty()) {
                emit();
                bytes.remove_prefix(1);
            }
        }
    }

    void finish() {
        if (!record_.empty())
            emit();
    }

    [[noreturn]] void error(const std::string& message) const { file_error(file_, line_, message); }

private:
    void emit() {
        try {
            consumer_(parse_msmarco_record(record_));
        } catch (const std::exception& error) {
            this->error(error.what());
        }
        record_.clear();
        ++line_;
    }

    const fs::path& file_;
    const DocumentConsumer& consumer_;
    std::string record_;
    std::size_t line_ = 1;
};

struct GzipState {
    z_stream stream{};

    GzipState(bool gzip, const Lines& lines) {
        if (gzip && inflateInit2(&stream, MAX_WBITS + 16) != Z_OK)
            lines.error("cannot initialize gzip decoder");
    }
    ~GzipState() {
        if (stream.state)
            inflateEnd(&stream);
    }
    GzipState(const GzipState&) = delete;
    GzipState& operator=(const GzipState&) = delete;

    void reset(const Lines& lines) {
        auto* next = stream.next_in;
        const auto available = stream.avail_in;
        if (inflateReset2(&stream, MAX_WBITS + 16) != Z_OK)
            lines.error("cannot reset gzip decoder");
        stream.next_in = next;
        stream.avail_in = available;
    }
};

void read_shard(std::ifstream& input, Lines& lines, bool gzip) {
    GzipState decoder(gzip, lines);
    auto& stream = decoder.stream;
    std::array<char, buffer_bytes> buffer{};
    std::array<char, buffer_bytes> expanded{};
    bool member_finished = false;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto size = static_cast<std::size_t>(input.gcount());
        if (input.bad() || (input.fail() && !input.eof()))
            lines.error(gzip ? "cannot read gzip file" : "cannot read JSONL file");
        if (!gzip) {
            lines.feed({buffer.data(), size});
            continue;
        }
        if (size == 0)
            break;
        stream.next_in = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_in = static_cast<uInt>(size);
        do {
            if (member_finished) {
                decoder.reset(lines);
                member_finished = false;
            }
            stream.next_out = reinterpret_cast<Bytef*>(expanded.data());
            stream.avail_out = static_cast<uInt>(expanded.size());
            const auto before = stream.avail_in;
            const int result = inflate(&stream, Z_NO_FLUSH);
            const auto produced = expanded.size() - stream.avail_out;
            if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR) {
                lines.error(std::string("invalid gzip: ") +
                            (stream.msg ? stream.msg : "decompression failed"));
            }
            lines.feed({expanded.data(), produced});
            if (result == Z_STREAM_END) {
                member_finished = true;
                if (stream.avail_in == 0)
                    break;
            } else if (before == stream.avail_in && produced == 0) {
                if (stream.avail_in != 0)
                    lines.error("gzip decoder made no progress");
                break;
            }
        } while (stream.avail_in != 0 || stream.avail_out == 0);
    }
    if (gzip && !member_finished)
        lines.error("truncated or empty gzip stream");
}

std::string lowercase_name(const fs::path& file) {
    auto name = file.filename().string();
    for (auto& ch : name)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return name;
}

bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool archive(const fs::path& file) {
    const auto name = lowercase_name(file);
    return ends_with(name, ".tar") || ends_with(name, ".tar.gz") || ends_with(name, ".tgz");
}

bool shard(const fs::path& file) {
    const auto name = lowercase_name(file);
    return ends_with(name, ".jsonl") || ends_with(name, ".gz");
}

std::vector<fs::path> shards(const fs::path& input) {
    std::vector<fs::path> files;
    try {
        if (fs::is_regular_file(input)) {
            if (archive(input))
                file_error(input, 1, "extract the outer TAR archive first; keep its gzip shards");
            if (!shard(input))
                file_error(input, 1, "expected a .jsonl or .gz shard");
            return {input};
        }
        if (!fs::is_directory(input))
            file_error(input, 1, "input is not a file or directory");
        for (const auto& entry : fs::recursive_directory_iterator(input)) {
            if (entry.is_symlink() || !entry.is_regular_file())
                continue;
            if (archive(entry.path())) {
                if (ends_with(lowercase_name(entry.path()), ".gz")) {
                    file_error(entry.path(), 1,
                               "extract the outer TAR archive first; keep its gzip shards");
                }
                continue;
            }
            if (shard(entry.path()))
                files.push_back(entry.path());
        }
    } catch (const fs::filesystem_error& error) {
        file_error(input, 1, error.what());
    }
    std::sort(files.begin(), files.end(), [&](const auto& left, const auto& right) {
        return left.lexically_relative(input).generic_string() <
               right.lexically_relative(input).generic_string();
    });
    if (files.empty())
        file_error(input, 1, "directory has no .jsonl or .gz shards");
    return files;
}

}

void read_msmarco(const fs::path& input, const DocumentConsumer& consumer) {
    if (!consumer)
        file_error(input, 1, "document consumer is empty");
    for (const auto& file : shards(input)) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            file_error(file, 1, "cannot open input shard");
        Lines lines(file, consumer);
        read_shard(stream, lines, ends_with(lowercase_name(file), ".gz"));
        lines.finish();
    }
}

}
