#include "query/cursors.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace search::detail {

PostingCursor::PostingCursor(const Index& index, std::optional<Term> term) : index_(index) {
    if (term) {
        if (term->begin > index_.posting_count() ||
            term->count > index_.posting_count() - term->begin) {
            throw std::runtime_error("Повреждены границы списка документов");
        }
        position_ = term->begin;
        end_ = position_ + term->count;
    }
}

DocId PostingCursor::next() {
    if (position_ != end_) {
        ++position_;
    }
    return doc();
}

DocId PostingCursor::advance(DocId target) {
    if (target > doc()) {
        auto lower = position_ + 1;
        auto upper = lower;
        std::uint64_t step = 1;
        const auto remaining = end_ - position_;
        while (upper < end_ && index_.posting(upper) < target) {
            lower = upper + 1;
            if (step >= end_ - upper) {
                upper = end_;
                break;
            }
            upper += step;
            step = step <= remaining / 2 ? step * 2 : remaining;
        }
        auto last = upper < end_ ? upper + 1 : end_;
        while (lower < last) {
            const auto middle = lower + (last - lower) / 2;
            if (index_.posting(middle) < target)
                lower = middle + 1;
            else
                last = middle;
        }
        position_ = lower;
    }
    return doc();
}

BinaryCursor::BinaryCursor(Operation operation, std::unique_ptr<Cursor> left,
                           std::unique_ptr<Cursor> right)
    : operation_(operation), left_(std::move(left)), right_(std::move(right)) {
    align();
}

DocId BinaryCursor::next() {
    if (current_ != end_doc) {
        if (operation_ == Operation::Or) {
            if (left_->doc() == current_)
                left_->next();
            if (right_->doc() == current_)
                right_->next();
        } else {
            left_->next();
        }
        align();
    }
    return current_;
}

DocId BinaryCursor::advance(DocId target) {
    if (target > current_) {
        left_->advance(target);
        if (operation_ == Operation::Or)
            right_->advance(target);
        align();
    }
    return current_;
}

std::uint64_t BinaryCursor::cost() const {
    if (operation_ == Operation::Or) {
        const auto left = left_->cost();
        const auto right = right_->cost();
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return left > maximum - right ? maximum : left + right;
    }
    if (current_ == end_doc)
        return 0;
    return operation_ == Operation::And ? std::min(left_->cost(), right_->cost()) : left_->cost();
}

void BinaryCursor::align() {
    if (operation_ == Operation::Or) {
        current_ = std::min(left_->doc(), right_->doc());
        return;
    }
    if (operation_ == Operation::And && right_->cost() < left_->cost()) {
        std::swap(left_, right_);
    }
    while (left_->doc() != end_doc) {
        const DocId candidate = left_->doc();
        const DocId other = right_->advance(candidate);
        if ((operation_ == Operation::And && other == candidate) ||
            (operation_ == Operation::Difference && other != candidate)) {
            current_ = candidate;
            return;
        }
        if (operation_ == Operation::And) {
            if (other == end_doc)
                break;
            left_->advance(other);
        } else {
            left_->next();
        }
    }
    current_ = end_doc;
}

NotCursor::NotCursor(std::unique_ptr<Cursor> excluded, std::uint64_t document_count)
    : excluded_(std::move(excluded)), document_count_(document_count) {
    seek(0);
}

DocId NotCursor::next() {
    if (current_ != end_doc) {
        seek(static_cast<std::uint64_t>(current_) + 1);
    }
    return current_;
}

DocId NotCursor::advance(DocId target) {
    if (target > current_) {
        seek(target);
    }
    return current_;
}

std::uint64_t NotCursor::cost() const {
    return current_ == end_doc ? 0 : document_count_ - current_;
}

void NotCursor::seek(std::uint64_t candidate) {
    while (candidate < document_count_ &&
           excluded_->advance(static_cast<DocId>(candidate)) == static_cast<DocId>(candidate)) {
        ++candidate;
    }
    current_ = candidate < document_count_ ? static_cast<DocId>(candidate) : end_doc;
}

}
