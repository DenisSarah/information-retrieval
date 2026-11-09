#pragma once

#include "search/cursor.hpp"
#include "search/index.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace search::detail {

class PostingCursor final : public Cursor {
public:
    PostingCursor(const Index& index, std::optional<Term> term);
    DocId doc() const override { return position_ == end_ ? end_doc : index_.posting(position_); }
    DocId next() override;
    DocId advance(DocId target) override;
    std::uint64_t cost() const override { return end_ - position_; }

private:
    const Index& index_;
    std::uint64_t position_ = 0;
    std::uint64_t end_ = 0;
};

enum class Operation { And, Or, Difference };

class BinaryCursor final : public Cursor {
public:
    BinaryCursor(Operation operation, std::unique_ptr<Cursor> left, std::unique_ptr<Cursor> right);
    DocId doc() const override { return current_; }
    DocId next() override;
    DocId advance(DocId target) override;
    std::uint64_t cost() const override;

private:
    void align();

    Operation operation_;
    std::unique_ptr<Cursor> left_;
    std::unique_ptr<Cursor> right_;
    DocId current_ = end_doc;
};

class NotCursor final : public Cursor {
public:
    NotCursor(std::unique_ptr<Cursor> excluded, std::uint64_t document_count);
    DocId doc() const override { return current_; }
    DocId next() override;
    DocId advance(DocId target) override;
    std::uint64_t cost() const override;

private:
    void seek(std::uint64_t candidate);

    std::unique_ptr<Cursor> excluded_;
    std::uint64_t document_count_;
    DocId current_ = end_doc;
};

}
