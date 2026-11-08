#include "index/builder.hpp"

#include "index/assembly.hpp"
#include "index/binary_io.hpp"
#include "index/builder_runs.hpp"
#include "search/text.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace search {
namespace {

namespace fs = std::filesystem;

BuildOptions valid_options(BuildOptions options) {
    if (!options.memory_bytes || options.merge_fan_in < 2) {
        throw std::runtime_error(
            "Бюджет памяти должен быть положительным, merge fan-in — не меньше 2");
    }
    options.merge_fan_in = std::min<std::size_t>(options.merge_fan_in, 64);
    return options;
}

struct Workspace {
    explicit Workspace(const fs::path& output) {
        fs::create_directories(output.parent_path());
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto suffix =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                std::to_string(random()) + "." + std::to_string(attempt);
            auto candidate =
                output.parent_path() / (output.filename().string() + ".build." + suffix);
            if (fs::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Не удалось создать уникальный временный каталог индекса");
    }
    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

}

struct IndexBuilder::Impl {
    enum class State { Active, Failed, Finished };

    explicit Impl(fs::path output, BuildOptions options)
        : output_(fs::absolute(std::move(output))), options_(valid_options(options)),
          workspace_(output_), doc_data_path_(workspace_.path / "documents"),
          doc_offsets_path_(workspace_.path / "document-offsets"),
          doc_data_(detail::builder_output(doc_data_path_)),
          doc_offsets_(detail::builder_output(doc_offsets_path_)) {
        detail::write_number(doc_offsets_, 0, 8);
        detail::builder_check(doc_offsets_);
    }

    void require_active() const {
        if (state_ == State::Failed)
            throw std::runtime_error("Построение индекса прервано предыдущей ошибкой");
        if (state_ == State::Finished)
            throw std::runtime_error("Построение индекса уже завершено");
    }

    fs::path next_path(const std::string& prefix) {
        return workspace_.path / (prefix + "." + std::to_string(sequence_++));
    }

    using RunLevels = std::vector<std::vector<detail::BuilderRun>>;

    detail::BuilderRun merge_runs(std::vector<detail::BuilderRun>& runs) {
        const auto result = detail::merge_builder_runs(next_path("merged"), runs);
        for (const auto& entry : runs)
            fs::remove(entry.path);
        std::vector<detail::BuilderRun>().swap(runs);
        return result;
    }

    void add_run(detail::BuilderRun run, std::size_t level = 0) {
        for (;; ++level) {
            if (level == runs_.size())
                runs_.emplace_back();
            auto& runs = runs_[level];
            runs.push_back(std::move(run));
            std::uint64_t heads = 0;
            for (const auto& entry : runs) {
                heads =
                    detail::builder_add(heads, detail::builder_add(entry.max_record, 16 * 1024));
            }
            if (runs.size() < options_.merge_fan_in &&
                (runs.size() < 2 || heads <= options_.memory_bytes))
                return;
            run = merge_runs(runs);
        }
    }

    void flush() {
        if (ids_.empty())
            return;
        add_run(detail::write_builder_run(next_path("run"), terms_, ids_));
        terms_.clear();
        std::vector<std::string>().swap(ids_);
        memory_estimate_ = 0;
    }

    void add(Document document) {
        require_active();
        state_ = State::Failed;
        if (stats_.documents >= end_doc)
            throw std::runtime_error("Слишком много документов");
        if (document.external_id.empty() ||
            document.external_id.size() > detail::builder_record_limit) {
            throw std::runtime_error("Внешний ID должен быть непустым и не больше 64 MiB");
        }
        detail::checked_size32(document.title.size());
        detail::checked_size32(document.text.size());
        validate_utf8(document.external_id);
        validate_utf8(document.title);
        auto words = tokenize(document.text);
        std::sort(words.begin(), words.end());
        words.erase(std::unique(words.begin(), words.end()), words.end());
        for (const auto& word : words) {
            if (word.size() > detail::builder_record_limit) {
                throw std::runtime_error("Терм превышает ограничение 64 MiB");
            }
        }

        detail::write_string(doc_data_, document.external_id);
        detail::write_string(doc_data_, document.title);
        detail::write_string(doc_data_, document.text);
        const auto bytes = std::uint64_t{12} + document.external_id.size() + document.title.size() +
                           document.text.size();
        doc_bytes_ = detail::builder_add(doc_bytes_, bytes);
        detail::write_number(doc_offsets_, doc_bytes_, 8);
        detail::builder_check(doc_data_);
        detail::builder_check(doc_offsets_);

        for (auto& word : words) {
            auto [it, inserted] = terms_.try_emplace(std::move(word));
            if (inserted) {
                auto bytes_used = detail::builder_add(sizeof(detail::BuilderTerms::value_type),
                                                      4 * sizeof(void*) + 32);
                bytes_used =
                    detail::builder_add(bytes_used, detail::builder_add(it->first.capacity(), 1));
                memory_estimate_ = detail::builder_add(memory_estimate_, bytes_used);
            }
            const auto before = it->second.capacity();
            it->second.push_back(static_cast<DocId>(stats_.documents));
            memory_estimate_ = detail::builder_add(
                memory_estimate_,
                detail::builder_multiply(it->second.capacity() - before, sizeof(DocId)));
        }
        const auto before = ids_.capacity();
        ids_.push_back(std::move(document.external_id));
        memory_estimate_ =
            detail::builder_add(memory_estimate_, detail::builder_multiply(ids_.capacity() - before,
                                                                           sizeof(std::string)));
        memory_estimate_ =
            detail::builder_add(memory_estimate_, detail::builder_add(ids_.back().capacity(), 33));
        stats_.text_bytes = detail::builder_add(stats_.text_bytes, document.text.size());
        stats_.postings = detail::builder_add(stats_.postings, words.size());
        ++stats_.documents;
        if (memory_estimate_ >= options_.memory_bytes)
            flush();
        state_ = State::Active;
    }

    detail::BuilderRun finish_runs() {
        for (std::size_t level = 0; level < runs_.size(); ++level) {
            auto& runs = runs_[level];
            if (runs.empty())
                continue;
            auto run = runs.size() == 1 ? std::move(runs.front()) : merge_runs(runs);
            runs.clear();
            if (level + 1 == runs_.size()) {
                RunLevels().swap(runs_);
                return run;
            }
            add_run(std::move(run), level + 1);
        }
        return {};
    }

    BuildStats finish() {
        require_active();
        state_ = State::Failed;
        flush();
        detail::builder_close(doc_data_);
        detail::builder_close(doc_offsets_);
        const auto run = finish_runs();
        const auto term_offsets = next_path("term-offsets");
        const auto term_data = next_path("term-data");
        const auto postings = next_path("postings");
        const auto term_stats = detail::materialize_builder_terms(
            run.path.empty() ? nullptr : &run, stats_.documents, term_offsets, term_data, postings);
        if (term_stats.postings != stats_.postings)
            throw std::runtime_error("Потеря postings при слиянии индекса");
        stats_.terms = term_stats.terms;
        if (!run.path.empty())
            fs::remove(run.path);
        detail::assemble_builder_file(
            {doc_data_path_, doc_offsets_path_, term_offsets, term_data, postings}, stats_,
            doc_bytes_, term_stats.bytes);
        fs::rename(doc_data_path_, output_);
        state_ = State::Finished;
        return stats_;
    }

    fs::path output_;
    BuildOptions options_;
    Workspace workspace_;
    fs::path doc_data_path_;
    fs::path doc_offsets_path_;
    std::ofstream doc_data_;
    std::ofstream doc_offsets_;
    detail::BuilderTerms terms_;
    std::vector<std::string> ids_;
    RunLevels runs_;
    BuildStats stats_;
    std::uint64_t doc_bytes_ = 0;
    std::uint64_t memory_estimate_ = 0;
    std::uint64_t sequence_ = 0;
    State state_ = State::Active;
};

IndexBuilder::IndexBuilder(fs::path output, BuildOptions options)
    : impl_(std::make_unique<Impl>(std::move(output), options)) {
}

IndexBuilder::~IndexBuilder() = default;

void IndexBuilder::add(Document document) {
    impl_->add(std::move(document));
}

BuildStats IndexBuilder::finish() {
    return impl_->finish();
}

}
