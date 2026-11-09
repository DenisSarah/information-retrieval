#pragma once

#include "search/document.hpp"

#include <cstdint>

namespace search {

class Cursor {
public:
    virtual ~Cursor() = default;
    virtual DocId doc() const = 0;
    virtual DocId next() = 0;
    virtual DocId advance(DocId target) = 0;
    virtual std::uint64_t cost() const = 0;
};

}
