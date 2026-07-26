#include "yai.hpp"

#include <cstdlib>
#include <sstream>

namespace {

bool parse_u64_field(const std::string& field, const char* key, std::uintmax_t& out) {
    const std::string prefix = std::string(key) + "=";
    if (field.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string value = field.substr(prefix.size());
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::uintmax_t>(parsed);
    return true;
}

bool parse_optional_u64_or_dash(
    const std::string& field,
    const char* key,
    std::optional<std::uintmax_t>& out) {
    const std::string prefix = std::string(key) + "=";
    if (field.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string value = field.substr(prefix.size());
    if (value == "-") {
        out = std::nullopt;
        return true;
    }
    std::uintmax_t parsed = 0;
    if (!parse_u64_field(field, key, parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_rate_field(const std::string& field, double& out) {
    if (field.rfind("rate=", 0) != 0) {
        return false;
    }
    const std::string value = field.substr(5);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || parsed < 0.0) {
        return false;
    }
    out = parsed;
    return true;
}

} // namespace

std::string format_batch_progress_event(
    std::uintmax_t done,
    std::optional<std::uintmax_t> total,
    double rate_bps) {
    std::ostringstream out;
    out << "PROGRESS done=" << done << " total=";
    if (total.has_value()) {
        out << *total;
    } else {
        out << '-';
    }
    out << " rate=" << static_cast<std::uintmax_t>(rate_bps);
    return out.str();
}

std::string format_batch_progress_clear_event() {
    return "PROGRESS_CLEAR";
}

std::optional<BatchProgressEvent> parse_batch_progress_event(const std::string& line) {
    const std::string trimmed = trim(line);
    if (trimmed == "PROGRESS_CLEAR") {
        BatchProgressEvent ev;
        ev.kind = BatchProgressEvent::Kind::Clear;
        return ev;
    }
    if (trimmed.rfind("PROGRESS ", 0) != 0) {
        return std::nullopt;
    }

    std::istringstream in(trimmed.substr(9));
    std::string done_field;
    std::string total_field;
    std::string rate_field;
    if (!(in >> done_field >> total_field >> rate_field)) {
        return std::nullopt;
    }
    std::string extra;
    if (in >> extra) {
        return std::nullopt;
    }

    BatchProgressEvent ev;
    ev.kind = BatchProgressEvent::Kind::Progress;
    if (!parse_u64_field(done_field, "done", ev.done) ||
        !parse_optional_u64_or_dash(total_field, "total", ev.total) ||
        !parse_rate_field(rate_field, ev.rate_bps)) {
        return std::nullopt;
    }
    return ev;
}

int batch_event_fd() {
    const std::optional<std::string> raw = env_string("YAI_BATCH_EVENT_FD");
    if (!raw.has_value() || raw->empty()) {
        return -1;
    }
    char* end = nullptr;
    const long fd = std::strtol(raw->c_str(), &end, 10);
    if (end == raw->c_str() || *end != '\0' || fd < 0 || fd > 2147483647L) {
        return -1;
    }
    return static_cast<int>(fd);
}
