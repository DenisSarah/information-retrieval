#include "index/binary_io.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace search::detail {

[[noreturn]] void invalid_index() {
    throw std::runtime_error("Поврежденный или неподдерживаемый индекс");
}

std::uint32_t checked_size32(std::uintmax_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Слишком большое поле индекса (более 4 ГиБ)");
    }
    return static_cast<std::uint32_t>(size);
}

BinaryReader::BinaryReader(const std::filesystem::path& file) : input_(file, std::ios::binary) {
    if (!input_)
        throw std::runtime_error("Не удалось открыть индекс: " + file.string());
    remaining_ = std::filesystem::file_size(file);
}

void BinaryReader::bytes(char* data, std::size_t count) {
    if (count > remaining_)
        invalid_index();
    if (count && !input_.read(data, static_cast<std::streamsize>(count)))
        invalid_index();
    remaining_ -= count;
}

std::uint64_t BinaryReader::number(unsigned width) {
    std::array<char, 8> buffer{};
    bytes(buffer.data(), width);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(buffer[i])) << (8 * i);
    }
    return value;
}

void write_number(std::ostream& out, std::uint64_t value, unsigned width) {
    std::array<char, 8> buffer{};
    for (unsigned i = 0; i < width; ++i)
        buffer[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    out.write(buffer.data(), width);
}

void write_string(std::ostream& out, const std::string& value) {
    write_number(out, checked_size32(value.size()), 4);
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

}
