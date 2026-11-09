#include "tests/test_support.hpp"
#include "tests/testcases.hpp"

#include "import/msmarco.hpp"

#include <zlib.h>

#include <cstddef>
#include <filesystem>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace search_tests {
namespace {

std::string record(const std::string& id, const std::string& body = "Body") {
    return "{\"docid\":\"" + id +
           "\",\"url\":\"https://url-only.invalid/secret\","
           "\"title\":\"Title\",\"headings\":\"Heading\",\"body\":\"" +
           body + "\"}";
}

std::string with_extra(const std::string& extra) {
    const auto base = record("doc");
    return base.substr(0, base.size() - 1) + ',' + extra + '}';
}

std::vector<Document> imported(const fs::path& path) {
    std::vector<Document> documents;
    search::detail::read_msmarco(
        path, [&](Document document) { documents.push_back(std::move(document)); });
    return documents;
}

void import_error(const fs::path& input, const fs::path& expected_file, std::size_t line = 1,
                  const std::string& detail = {}) {
    try {
        search::detail::read_msmarco(input, [](Document) {});
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        const auto prefix = expected_file.string() + ':';
        if (line == 0) {
            const auto colon = message.find(':', prefix.size());
            bool contextual = message.compare(0, prefix.size(), prefix) == 0 &&
                              colon != std::string::npos && colon > prefix.size() &&
                              message[prefix.size()] >= '1' && message[prefix.size()] <= '9';
            for (auto i = prefix.size(); contextual && i < colon; ++i) {
                contextual = message[i] >= '0' && message[i] <= '9';
            }
            if (!contextual)
                fail("import error has a positive file:line", __LINE__, message);
        } else {
            const auto expected = prefix + std::to_string(line) + ':';
            if (message.find(expected) == std::string::npos) {
                fail("import error has expected file:line", __LINE__,
                     "expected " + expected + "; got " + message);
            }
        }
        if (!detail.empty())
            SEARCH_TEST_CHECK(message.find(detail) != std::string::npos);
        return;
    }
    fail("MS MARCO import throws a contextual runtime_error", __LINE__, input.string());
}

class GzipWriter {
public:
    explicit GzipWriter(const fs::path& path) : file_(gzopen(path.c_str(), "wb")) {
        SEARCH_TEST_CHECK(file_ != nullptr);
    }
    ~GzipWriter() {
        if (file_)
            gzclose(file_);
    }
    GzipWriter(const GzipWriter&) = delete;
    GzipWriter& operator=(const GzipWriter&) = delete;

    void append(const std::string& value) {
        const auto written = gzwrite(file_, value.data(), static_cast<unsigned>(value.size()));
        SEARCH_TEST_CHECK(written >= 0);
        SEARCH_TEST_CHECK(static_cast<std::size_t>(written) == value.size());
    }

    void finish() {
        const auto file = std::exchange(file_, nullptr);
        SEARCH_TEST_CHECK(gzclose(file) == Z_OK);
    }

private:
    gzFile file_ = nullptr;
};

void gzip_file(const fs::path& path, const std::string& text) {
    GzipWriter output(path);
    output.append(text);
    output.finish();
}

void plain_records_and_unicode(const fs::path& root) {
    const auto file = root / "unicode.jsonl";
    const std::string escaped =
        R"json({"docid":"doc\/\u0031","url":"https:\/\/url-only.invalid","title":"\u041A\u043E\u0442 \uD83D\uDE00","headings":"H1\nH2","body":"\"quoted\"\\slash\/\b\f\n\r\t\u0000"})json";
    write_bytes(file, " \t" + escaped + " \r\n" +
                          R"json({"body":"body","headings":"","title":"","docid":"second"})json");
    const auto documents = imported(file);
    SEARCH_TEST_CHECK(documents.size() == 2);
    SEARCH_TEST_CHECK(documents[0].external_id == "doc/1");
    SEARCH_TEST_CHECK(documents[0].title == "Кот 😀");
    auto expected = std::string("Кот 😀\nH1\nH2\n\"quoted\"\\slash/\b\f\n\r\t");
    expected += '\0';
    SEARCH_TEST_CHECK(documents[0].text == expected);
    SEARCH_TEST_CHECK(documents[0].text.find("url-only") == std::string::npos);
    SEARCH_TEST_CHECK(documents[1].external_id == "second");
    SEARCH_TEST_CHECK(documents[1].title.empty());
    SEARCH_TEST_CHECK(documents[1].text == "body");

    write_bytes(
        file,
        with_extra(
            R"json("ignored":[true,false,null,-0,12,-1.25e+3,1E-2,1e99999,{"x":"\uDBFF\uDFFF"}])json"));
    SEARCH_TEST_CHECK(imported(file)[0].text == "Title\nHeading\nBody");
    write_bytes(file, "");
    SEARCH_TEST_CHECK(imported(file).empty());

    std::mt19937 random(0xcafef00du);
    std::string long_body(300000, 'a');
    for (auto& ch : long_body)
        ch = static_cast<char>('a' + random() % 26);
    write_bytes(file, record("long", long_body));
    SEARCH_TEST_CHECK(imported(file)[0].text == "Title\nHeading\n" + long_body);
    const auto gz = root / "long.jsonl.gz";
    gzip_file(gz, record("long", long_body));
    SEARCH_TEST_CHECK(read_bytes(gz).size() > 65536);
    SEARCH_TEST_CHECK(imported(gz)[0].text == "Title\nHeading\n" + long_body);
}

void invalid_json_and_schema(const fs::path& root) {
    const auto file = root / "invalid.jsonl";
    const std::vector<std::string> invalid{
        "",
        " ",
        "[]",
        "null",
        "{}",
        record(""),
        record("doc") + " {}",
        R"json({"docid":1,"title":"","headings":"","body":""})json",
        R"json({"docid":"x","title":null,"headings":"","body":""})json",
        R"json({"docid":"x","title":"","headings":[],"body":""})json",
        R"json({"docid":"x","title":"","headings":"","body":false})json",
        R"json({"docid":"x","title":"","headings":""})json",
        with_extra(R"json("docid":"again")json"),
        with_extra(R"json("\u0064ocid":"again")json"),
        with_extra(R"json("x":1,"x":2)json"),
        with_extra(R"json("x":{"a":1,"\u0061":2})json"),
        with_extra(R"json("x":[true false])json"),
        with_extra(R"json("x":[1,])json"),
        with_extra(R"json("x":{"a":1,})json"),
        with_extra(R"json("x":"\q")json"),
        with_extra(R"json("x":"\u12xz")json"),
        with_extra(R"json("x":"\u123")json"),
        with_extra(R"json("x":"\uD800")json"),
        with_extra(R"json("x":"\uDC00")json"),
        with_extra(R"json("x":"\uD800\u0041")json"),
        with_extra(R"json("x":"\uD800\uDBFF")json"),
        with_extra("\"x\":\"raw\tcontrol\""),
        with_extra(std::string("\"x\":\"") + '\xc2' + "\""),
    };
    for (const auto& value : invalid) {
        write_bytes(file, value + '\n');
        import_error(file, file);
    }
    for (const auto& number : {"01", "-01", "1.", ".1", "1e", "1e+", "+1", "NaN", "Infinity", "--1",
                               "tru", "falsex", "nullx"}) {
        write_bytes(file, with_extra(std::string("\"number\":") + number));
        import_error(file, file);
    }
    write_bytes(file, record("first") + "\n" + with_extra(R"json("body":"duplicate")json"));
    import_error(file, file, 2, "duplicate");

    const auto nested = [](unsigned arrays) {
        return with_extra("\"deep\":" + std::string(arrays, '[') + "null" +
                          std::string(arrays, ']'));
    };
    write_bytes(file, nested(63));
    SEARCH_TEST_CHECK(imported(file).size() == 1);
    write_bytes(file, nested(64));
    import_error(file, file, 1, "nesting");

    write_bytes(file, record("same") + '\n' + record("same"));
    SEARCH_TEST_CHECK(imported(file).size() == 2);
    std::set<std::string> seen;
    bool caught = false;
    try {
        search::detail::read_msmarco(file, [&](Document document) {
            if (!seen.insert(document.external_id).second)
                throw std::runtime_error("duplicate external docid");
        });
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        SEARCH_TEST_CHECK(message.find(file.string() + ":2:") != std::string::npos);
        SEARCH_TEST_CHECK(message.find("duplicate external docid") != std::string::npos);
        caught = true;
    }
    SEARCH_TEST_CHECK(caught);
    expect_error([&] { search::detail::read_msmarco(file, {}); });
}

void gzip_integrity(const fs::path& root) {
    const auto first = root / "first.gz";
    const auto second = root / "second.gz";
    const auto combined = root / "combined.jsonl.gz";
    gzip_file(first, record("first") + '\n');
    gzip_file(second, record("second"));
    const auto bytes = read_bytes(first);
    write_bytes(combined, bytes + read_bytes(second));
    const auto records = imported(combined);
    SEARCH_TEST_CHECK(records.size() == 2);
    SEARCH_TEST_CHECK(records[0].external_id == "first");
    SEARCH_TEST_CHECK(records[1].external_id == "second");
    gzip_file(second, "");
    SEARCH_TEST_CHECK(imported(second).empty());
    const auto empty_member = read_bytes(second);
    write_bytes(combined, empty_member + bytes + empty_member);
    SEARCH_TEST_CHECK(imported(combined).size() == 1);

    const auto split = record("split");
    const auto middle = split.size() / 2;
    gzip_file(first, split.substr(0, middle));
    gzip_file(second, split.substr(middle));
    write_bytes(combined, read_bytes(first) + read_bytes(second));
    const auto split_document = imported(combined);
    SEARCH_TEST_CHECK(split_document.size() == 1);
    SEARCH_TEST_CHECK(split_document[0].external_id == "split");
    SEARCH_TEST_CHECK(split_document[0].text == "Title\nHeading\nBody");

    const auto bad = root / "bad.gz";
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        write_bytes(bad, bytes.substr(0, length));
        import_error(bad, bad, 0);
    }
    SEARCH_TEST_CHECK(bytes.size() > 18);
    for (const auto position : {std::size_t{0}, bytes.size() - 8, bytes.size() - 4}) {
        auto corrupted = bytes;
        corrupted[position] =
            static_cast<char>(static_cast<unsigned char>(corrupted[position]) ^ 0x80u);
        write_bytes(bad, corrupted);
        import_error(bad, bad, 0);
    }
    write_bytes(bad, bytes + "junk");
    import_error(bad, bad, 0);
    write_bytes(bad, bytes + std::string(1, 'x'));
    import_error(bad, bad, 0);
    write_bytes(bad, bytes + std::string("\x1f", 1));
    import_error(bad, bad, 0);
    write_bytes(bad, record("not-gzip"));
    import_error(bad, bad, 0);

    const auto oversized = root / "oversized.gz";
    GzipWriter output(oversized);
    const std::string chunk(65536, ' ');
    for (unsigned i = 0; i < 1025; ++i)
        output.append(chunk);
    output.finish();
    import_error(oversized, oversized, 1, "64 MiB");
}

void directory_shards(const fs::path& root) {
    const auto directory = root / "shards";
    fs::create_directories(directory / "nested");
    fs::create_directories(root / "outside");
    gzip_file(directory / "a.jsonl.gz", record("a"));
    gzip_file(directory / "nested" / "msmarco_doc_32.gz", record("b"));
    write_bytes(directory / "z.jsonl", record("z"));
    write_bytes(directory / "ignored.txt", "\xff");
    write_bytes(root / "outside" / "leak.jsonl", record("leak"));
    fs::create_symlink(root / "outside" / "leak.jsonl", directory / "link.jsonl");
    fs::create_directory_symlink(directory, directory / "loop");
    const auto documents = imported(directory);
    SEARCH_TEST_CHECK(documents.size() == 3);
    SEARCH_TEST_CHECK(documents[0].external_id == "a");
    SEARCH_TEST_CHECK(documents[1].external_id == "b");
    SEARCH_TEST_CHECK(documents[2].external_id == "z");

    import_error(root / "missing.jsonl", root / "missing.jsonl");
    import_error(directory / "ignored.txt", directory / "ignored.txt");
    fs::create_directory(root / "empty");
    import_error(root / "empty", root / "empty");
    const auto archive = root / "outer.tar.gz";
    write_bytes(archive, "not unpacked");
    import_error(archive, archive, 1, "extract");
}

}

void test_msmarco_import() {
    TemporaryDirectory temporary;
    plain_records_and_unicode(temporary.path());
    invalid_json_and_schema(temporary.path());
    gzip_integrity(temporary.path());
    directory_shards(temporary.path());
}

}
