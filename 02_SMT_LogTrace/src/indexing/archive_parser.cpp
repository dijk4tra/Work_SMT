/**
 * @file archive_parser.cpp
 * @brief 实现流式完整性回验、运行日志和测试报告解析。
 */

#include "logtrace/indexing/archive_parser.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "logtrace/common/sha256.h"
#include "logtrace/common/time_utils.h"

namespace smt {
namespace logtrace {
namespace {

struct LineView {
    std::uint64_t number;
    std::uint64_t offset;
    std::uint64_t length;
    std::string text;
};

struct ParserState {
    ArchiveParseResult result;
    std::map<std::string, std::string> fct_header;
    std::string fct_occurred_at;
    bool fct_table_header_seen;
};

bool validUtf8(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7F) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
            codepoint = first & 0x1F;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
            codepoint = first & 0x0F;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (index + length > value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char next = static_cast<unsigned char>(value[index + offset]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
            return false;
        }
        index += length;
    }
    return true;
}

std::size_t countTerms(const std::string& text) {
    std::size_t count = 0;
    bool in_term = false;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        const unsigned char value = static_cast<unsigned char>(*it);
        const bool term_character =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-' || value >= 0x80;
        if (term_character && !in_term) {
            ++count;
        }
        in_term = term_character;
    }
    return count;
}

void fail(ParserState* state, const std::string& code, std::uint64_t line) {
    if (state->result.success) {
        state->result.success = false;
        state->result.failure_code = code;
        state->result.failure_line = line;
        state->result.documents.clear();
    }
}

bool parseCsvRow(const std::string& line, std::vector<std::string>* fields) {
    fields->clear();
    std::string field;
    bool quoted = false;
    bool quote_closed = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char value = line[index];
        if (quoted) {
            if (value == '"') {
                if (index + 1 < line.size() && line[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    quoted = false;
                    quote_closed = true;
                }
            } else {
                field.push_back(value);
            }
            continue;
        }
        if (quote_closed) {
            if (value != ',') {
                return false;
            }
            fields->push_back(field);
            field.clear();
            quote_closed = false;
            continue;
        }
        if (value == ',') {
            fields->push_back(field);
            field.clear();
        } else if (value == '"') {
            if (!field.empty()) {
                return false;
            }
            quoted = true;
        } else {
            field.push_back(value);
        }
    }
    if (quoted) {
        return false;
    }
    fields->push_back(field);
    return true;
}

bool validLevel(const std::string& level) {
    static const std::set<std::string> levels = {"TRACE", "DEBUG", "INFO",
                                                 "WARN",  "ERROR", "CRITICAL"};
    return levels.count(level) != 0;
}

bool parseFiniteNumber(const std::string& value, double* result) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    *result = std::strtod(value.c_str(), &end);
    return errno == 0 && end == value.c_str() + value.size() && std::isfinite(*result);
}

bool isFiniteNumber(const std::string& value) {
    double result = 0.0;
    return parseFiniteNumber(value, &result);
}

ParsedDocument baseDocument(const ArchiveRecord& archive, const LineView& line) {
    ParsedDocument document;
    document.archive_id = archive.archive_id;
    document.byte_offset = line.offset;
    document.byte_length = line.length;
    document.archived_at = archive.archived_at;
    document.line_id = archive.line_id;
    document.station_id = archive.station_id;
    document.device_id = archive.device_id;
    document.collector_id = archive.collector_id;
    document.work_order = archive.work_order;
    document.product_sn = archive.product_sn;
    document.source_type = archive.file_type;
    document.term_count = countTerms(line.text);
    return document;
}

void parseRuntimeLine(const ArchiveRecord& archive, const LineView& line, ParserState* state) {
    if (line.text.empty() || !validUtf8(line.text)) {
        fail(state, line.text.empty() ? "RUNTIME_LOG_FORMAT_INVALID" : "INVALID_UTF8", line.number);
        return;
    }
    const std::size_t first_space = line.text.find(' ');
    if (first_space == std::string::npos) {
        fail(state, "RUNTIME_LOG_FORMAT_INVALID", line.number);
        return;
    }
    std::int64_t occurred_milliseconds = 0;
    if (!parseIso8601Milliseconds(line.text.substr(0, first_space), &occurred_milliseconds)) {
        fail(state, "RUNTIME_LOG_TIME_INVALID", line.number);
        return;
    }

    std::map<std::string, std::string> fields;
    std::size_t begin = first_space + 1;
    while (begin < line.text.size()) {
        const std::size_t end = line.text.find(' ', begin);
        const std::string token =
            line.text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const std::size_t separator = token.find('=');
        if (token.empty() || separator == std::string::npos || separator == 0 ||
            separator + 1 == token.size() || token.find('=', separator + 1) != std::string::npos ||
            fields.count(token.substr(0, separator)) != 0) {
            fail(state, "RUNTIME_LOG_FORMAT_INVALID", line.number);
            return;
        }
        fields[token.substr(0, separator)] = token.substr(separator + 1);
        begin = end == std::string::npos ? line.text.size() : end + 1;
    }
    if (fields.count("level") == 0 || fields.count("module") == 0 || !validLevel(fields["level"])) {
        fail(state, "RUNTIME_LOG_FORMAT_INVALID", line.number);
        return;
    }
    if (fields.count("device") != 0 && fields["device"] != archive.device_id) {
        fail(state, "RUNTIME_LOG_METADATA_MISMATCH", line.number);
        return;
    }
    if (fields.count("source_device") != 0 && fields["source_device"] != archive.device_id) {
        fail(state, "RUNTIME_LOG_METADATA_MISMATCH", line.number);
        return;
    }
    if (fields.count("station") != 0 && fields["station"] != archive.station_id) {
        fail(state, "RUNTIME_LOG_METADATA_MISMATCH", line.number);
        return;
    }
    if (fields.count("sn") != 0 && !archive.product_sn.empty() &&
        fields["sn"] != archive.product_sn) {
        fail(state, "RUNTIME_LOG_METADATA_MISMATCH", line.number);
        return;
    }

    ParsedDocument document = baseDocument(archive, line);
    document.occurred_at = formatUtcMilliseconds(occurred_milliseconds);
    document.level = fields["level"];
    document.module_name = fields["module"];
    document.error_code = fields.count("code") == 0 || fields["code"] == "-" ? "" : fields["code"];
    document.event_name = fields.count("event") == 0 ? "" : fields["event"];
    if (fields.count("sn") != 0) {
        document.product_sn = fields["sn"];
    }
    state->result.documents.push_back(document);
}

void parseFctLine(const ArchiveRecord& archive, const LineView& line, ParserState* state) {
    if (line.text.empty() || !validUtf8(line.text)) {
        fail(state, line.text.empty() ? "FCT_CSV_FORMAT_INVALID" : "INVALID_UTF8", line.number);
        return;
    }
    std::string text = line.text;
    if (line.number == 1 && text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    std::vector<std::string> fields;
    if (!parseCsvRow(text, &fields)) {
        fail(state, "FCT_CSV_FORMAT_INVALID", line.number);
        return;
    }

    static const char* kHeaderKeys[] = {"ReportVersion", "DeviceId", "WorkOrder",
                                        "ProductSN",     "TestedAt", "OverallResult"};
    if (line.number <= 6) {
        if (fields.size() != 2 || fields[0] != kHeaderKeys[line.number - 1] || fields[1].empty()) {
            fail(state, "FCT_CSV_HEADER_INVALID", line.number);
            return;
        }
        state->fct_header[fields[0]] = fields[1];
        if (line.number == 6) {
            std::int64_t occurred_milliseconds = 0;
            if (state->fct_header["DeviceId"] != archive.device_id ||
                state->fct_header["ReportVersion"] != "1.2" ||
                (!archive.work_order.empty() &&
                 state->fct_header["WorkOrder"] != archive.work_order) ||
                (!archive.product_sn.empty() &&
                 state->fct_header["ProductSN"] != archive.product_sn) ||
                !parseIso8601Milliseconds(state->fct_header["TestedAt"], &occurred_milliseconds) ||
                (state->fct_header["OverallResult"] != "PASS" &&
                 state->fct_header["OverallResult"] != "NG")) {
                fail(state, "FCT_CSV_METADATA_MISMATCH", line.number);
                return;
            }
            state->fct_occurred_at = formatUtcMilliseconds(occurred_milliseconds);
        }
        return;
    }

    if (line.number == 7) {
        static const char* kColumns[] = {"TestPoint", "TestName", "LowerLimit", "UpperLimit",
                                         "Measured",  "Unit",     "Result"};
        if (fields.size() != 7) {
            fail(state, "FCT_CSV_HEADER_INVALID", line.number);
            return;
        }
        for (std::size_t index = 0; index < fields.size(); ++index) {
            if (fields[index] != kColumns[index]) {
                fail(state, "FCT_CSV_HEADER_INVALID", line.number);
                return;
            }
        }
        state->fct_table_header_seen = true;
        return;
    }

    double lower_limit = 0.0;
    double upper_limit = 0.0;
    if (!state->fct_table_header_seen || fields.size() != 7 || fields[0].empty() ||
        fields[1].empty() || fields[2].empty() || fields[3].empty() || fields[4].empty() ||
        fields[5].empty() || !parseFiniteNumber(fields[2], &lower_limit) ||
        !parseFiniteNumber(fields[3], &upper_limit) || !isFiniteNumber(fields[4]) ||
        lower_limit > upper_limit || (fields[6] != "PASS" && fields[6] != "NG")) {
        fail(state, "FCT_CSV_ROW_INVALID", line.number);
        return;
    }

    ParsedDocument document = baseDocument(archive, line);
    document.occurred_at = state->fct_occurred_at;
    document.work_order = state->fct_header["WorkOrder"];
    document.product_sn = state->fct_header["ProductSN"];
    document.level = fields[6] == "NG" ? "ERROR" : "INFO";
    document.module_name = "fct";
    document.error_code = fields[6] == "NG" ? "FCT_LIMIT_FAIL" : "";
    document.event_name = fields[0] + ":" + fields[1];
    state->result.documents.push_back(document);
}

void processLine(const ArchiveRecord& archive, const ParserProfile& profile, const LineView& line,
                 ParserState* state) {
    if (!state->result.success) {
        return;
    }
    if (profile.name == "kv_runtime_v1") {
        parseRuntimeLine(archive, line, state);
    } else if (profile.name == "fct_csv_v1") {
        parseFctLine(archive, line, state);
    } else {
        fail(state, "PARSER_PROFILE_UNSUPPORTED", 0);
    }
}

void finishParser(const ParserProfile& profile, ParserState* state) {
    if (!state->result.success) {
        return;
    }
    if (profile.name == "kv_runtime_v1" && state->result.documents.empty()) {
        fail(state, "RUNTIME_LOG_EMPTY", 0);
    } else if (profile.name == "fct_csv_v1" &&
               (!state->fct_table_header_seen || state->result.documents.empty())) {
        fail(state, "FCT_CSV_EMPTY", 0);
    }
}

}  // namespace

ArchiveParseResult parseArchive(const ArchiveRecord& archive, const std::string& absolute_path,
                                const ParserProfile& profile, std::size_t max_line_bytes) {
    ParserState state;
    state.result.success = true;
    state.result.failure_line = 0;
    state.fct_table_header_seen = false;
    if (profile.name != "kv_runtime_v1" && profile.name != "fct_csv_v1") {
        fail(&state, "PARSER_PROFILE_UNSUPPORTED", 0);
        return state.result;
    }

    const int fd = ::open(absolute_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fail(&state, errno == ENOENT ? "ARCHIVE_FILE_NOT_FOUND" : "ARCHIVE_FILE_OPEN_FAILED", 0);
        return state.result;
    }

    Sha256 sha256;
    char buffer[65536];
    std::string line;
    bool line_overflow = false;
    std::uint64_t absolute_offset = 0;
    std::uint64_t line_offset = 0;
    std::uint64_t line_number = 1;
    bool read_failed = false;

    for (;;) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            read_failed = true;
            break;
        }
        sha256.update(buffer, static_cast<std::size_t>(count));
        for (ssize_t index = 0; index < count; ++index, ++absolute_offset) {
            if (buffer[index] == '\n') {
                std::uint64_t length = line.size();
                if (!line.empty() && line[line.size() - 1] == '\r') {
                    line.resize(line.size() - 1);
                    --length;
                }
                if (line_overflow || length > max_line_bytes) {
                    fail(&state, "LINE_TOO_LONG", line_number);
                } else {
                    processLine(archive, profile, LineView{line_number, line_offset, length, line},
                                &state);
                }
                line.clear();
                line_overflow = false;
                line_offset = absolute_offset + 1;
                ++line_number;
            } else if (line.size() <= max_line_bytes) {
                line.push_back(buffer[index]);
            } else {
                line_overflow = true;
            }
        }
    }

    const int close_result = ::close(fd);
    if (read_failed || close_result != 0) {
        state.result.success = false;
        state.result.failure_code =
            read_failed ? "ARCHIVE_FILE_READ_FAILED" : "ARCHIVE_FILE_CLOSE_FAILED";
        state.result.failure_line = 0;
        state.result.documents.clear();
        return state.result;
    }
    if (!line.empty() || line_overflow) {
        std::uint64_t length = line.size();
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
            --length;
        }
        if (line_overflow || length > max_line_bytes) {
            fail(&state, "LINE_TOO_LONG", line_number);
        } else {
            processLine(archive, profile, LineView{line_number, line_offset, length, line}, &state);
        }
    }

    const std::string actual_sha256 = sha256.finishHex();
    if (absolute_offset != archive.file_size) {
        state.result.success = false;
        state.result.failure_code = "ARCHIVE_SIZE_MISMATCH";
        state.result.failure_line = 0;
        state.result.documents.clear();
        return state.result;
    }
    if (actual_sha256 != archive.file_sha256) {
        state.result.success = false;
        state.result.failure_code = "ARCHIVE_SHA256_MISMATCH";
        state.result.failure_line = 0;
        state.result.documents.clear();
        return state.result;
    }
    finishParser(profile, &state);
    return state.result;
}

}  // namespace logtrace
}  // namespace smt
