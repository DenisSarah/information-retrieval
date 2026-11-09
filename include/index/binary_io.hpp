#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

namespace search::detail {

[[noreturn]] void invalid_index();
std::uint32_t checked_size32(std::uintmax_t size);

class BinaryReader {
public:
    explicit BinaryReader(const std::filesystem::path& file);
    void bytes(char* data, std::size_t count);
    std::uint64_t number(unsigned width);
    std::uint64_t remaining() const { return remaining_; }

private:
    std::ifstream input_;
    std::uint64_t remaining_ = 0;
};

void write_number(std::ostream& out, std::uint64_t value, unsigned width);
void write_string(std::ostream& out, const std::string& value);

}
