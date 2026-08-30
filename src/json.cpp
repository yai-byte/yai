#include "yai.hpp"

// This is a deliberately small scanner for yai's current metadata, repo-index,
// AppImage feed, and GitHub latest-release shapes. Keep inputs constrained to
// well-formed JSON where the needed fields are strings, positive integers,
// objects, or arrays; switch to a real JSON library before accepting broader or
// adversarial JSON semantics such as unicode escapes, floats, booleans, or nulls.
struct JsonStringScanState {
    // Bracket matching must ignore braces and brackets inside strings. The
    // escaped flag keeps an escaped quote from closing the string state.
    bool in_string = false;
    bool escaped = false;
};

bool advance_json_string_state(JsonStringScanState& state, char ch) {
    // Return true when this character belongs to JSON string syntax. Callers can
    // then skip structural handling for quotes, backslashes, and punctuation
    // inside a string value.
    if (state.in_string) {
        if (state.escaped) {
            state.escaped = false;
        } else if (ch == '\\') {
            state.escaped = true;
        } else if (ch == '"') {
            state.in_string = false;
        }
        return true;
    }

    if (ch == '"') {
        state.in_string = true;
        return true;
    }
    return false;
}

char json_simple_escape(char ch) {
    if (ch == 'n') {
        return '\n';
    }
    if (ch == 'r') {
        return '\r';
    }
    if (ch == 't') {
        return '\t';
    }
    return ch;
}

// Encodes a Unicode code point as UTF-8 bytes. Surrogate halves from
// \uXXXX escapes are each emitted as their own 3-byte CESU-8 sequence;
// combining them into a single 4-byte UTF-8 sequence would require
// tracking surrogate pairs across calls, which is unnecessary for yai's
// text fields (names, summaries, homepages).
void append_utf8(std::string& out, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string json_unescape_string(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out.push_back(value[i]);
            continue;
        }
        const char next = value[++i];
        if (next == '"' || next == '\\' || next == '/') {
            out.push_back(next);
        } else if (next == 'u' && i + 4 < value.size()) {
            // \uXXXX → UTF-8. Four hex digits follow; on success advance past
            // them, on malformed input keep the literal backslash-u.
            const std::string hex = value.substr(i + 1, 4);
            if (std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
                    return std::isxdigit(c) != 0;
                })) {
                unsigned codepoint = 0;
                std::istringstream ss(hex);
                ss >> std::hex >> codepoint;
                append_utf8(out, codepoint);
                i += 4;
            } else {
                out.push_back('\\');
                out.push_back('u');
            }
        } else {
            out.push_back(json_simple_escape(next));
        }
    }
    return out;
}

// Finds the first occurrence of needle that is NOT inside a JSON string
// literal. Naive text.find() would match punctuation inside string values
// (e.g. a summary containing the text "id": "x"), producing wrong key hits.
std::size_t json_find_key_outside_strings(const std::string& text, const std::string& needle, std::size_t from = 0) {
    JsonStringScanState string_state;
    for (std::size_t pos = from; pos + needle.size() <= text.size(); ) {
        // Advance string state up to pos so we know whether pos is inside a string.
        // We scan character-by-character; advance_json_string_state handles quotes.
        const char ch = text[pos];
        if (advance_json_string_state(string_state, ch)) {
            ++pos;
            continue;
        }
        // pos is outside a string — check for the needle here.
        if (text.compare(pos, needle.size(), needle) == 0) {
            return pos;
        }
        ++pos;
    }
    return std::string::npos;
}

std::optional<std::string> json_string_after(const std::string& text, std::size_t key_pos) {
    // This scanner only accepts a quoted string as the direct value after the
    // key. Numbers, booleans, null, arrays, and objects intentionally return
    // nullopt instead of being coerced into strings.
    const std::size_t colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t quote = colon + 1;
    while (quote < text.size() && std::isspace(static_cast<unsigned char>(text[quote]))) {
        ++quote;
    }
    if (quote >= text.size() || text[quote] != '"') {
        return std::nullopt;
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = quote + 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            value.push_back('\\');
            value.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return json_unescape_string(value);
        } else {
            value.push_back(ch);
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_find_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t key_pos = json_find_key_outside_strings(text, needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    return json_string_after(text, key_pos);
}

std::vector<std::string> json_find_all_strings(const std::string& text, const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json_find_key_outside_strings(text, needle, pos)) != std::string::npos) {
        std::optional<std::string> value = json_string_after(text, pos);
        if (value.has_value()) {
            values.push_back(*value);
        }
        pos += needle.size();
    }
    return values;
}

std::optional<std::size_t> json_value_start_after_key(const std::string& text, const std::string& key) {
    // Field lookup is intentionally shallow and name-based. yai's current JSON
    // inputs do not require JSONPath, duplicate-key policy, or scoped object
    // traversal beyond passing an already extracted object string.
    const std::string needle = "\"" + key + "\"";
    const std::size_t key_pos = json_find_key_outside_strings(text, needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t pos = colon + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos < text.size() ? std::optional<std::size_t>{pos} : std::nullopt;
}

std::optional<std::string> json_extract_balanced(
    const std::string& text,
    std::size_t start,
    char open_ch,
    char close_ch) {
    if (start >= text.size() || text[start] != open_ch) {
        return std::nullopt;
    }

    int depth = 0;
    JsonStringScanState string_state;
    // Depth only changes outside strings, so nested objects/arrays are preserved
    // while quoted punctuation stays part of the value being scanned.
    for (std::size_t pos = start; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (advance_json_string_state(string_state, ch)) {
            continue;
        }

        if (ch == open_ch) {
            ++depth;
        } else if (ch == close_ch) {
            --depth;
            if (depth == 0) {
                return text.substr(start, pos - start + 1);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_find_balanced_value(
    const std::string& text,
    const std::string& key,
    char open_ch,
    char close_ch) {
    const std::optional<std::size_t> start = json_value_start_after_key(text, key);
    if (!start.has_value()) {
        return std::nullopt;
    }
    return json_extract_balanced(text, *start, open_ch, close_ch);
}

std::optional<std::string> json_find_object(const std::string& text, const std::string& key) {
    return json_find_balanced_value(text, key, '{', '}');
}

std::optional<std::string> json_find_array(const std::string& text, const std::string& key) {
    return json_find_balanced_value(text, key, '[', ']');
}

std::map<std::string, std::string> json_find_string_map(const std::string& object_text, const std::string& key) {
    // Extract "key": { "a": "b", ... } and collect only top-level string values.
    // Nested objects/arrays are skipped; non-string values are ignored.
    std::map<std::string, std::string> values;
    const std::optional<std::string> object = json_find_object(object_text, key);
    if (!object.has_value() || object->size() < 2 || object->front() != '{') {
        return values;
    }

    const std::string& text = *object;
    JsonStringScanState string_state;
    int depth = 0;
    for (std::size_t pos = 0; pos < text.size(); ++pos) {
        const char ch = text[pos];

        // Candidate map key: opening quote at object depth 1, outside any string.
        if (!string_state.in_string && depth == 1 && ch == '"') {
            std::string map_key;
            bool escaped = false;
            std::size_t key_end = pos + 1;
            for (; key_end < text.size(); ++key_end) {
                const char kc = text[key_end];
                if (escaped) {
                    map_key.push_back('\\');
                    map_key.push_back(kc);
                    escaped = false;
                } else if (kc == '\\') {
                    escaped = true;
                } else if (kc == '"') {
                    break;
                } else {
                    map_key.push_back(kc);
                }
            }
            if (key_end >= text.size()) {
                break;
            }

            std::size_t colon = key_end + 1;
            while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
                ++colon;
            }
            if (colon < text.size() && text[colon] == ':') {
                map_key = json_unescape_string(map_key);
                const std::optional<std::string> map_value = json_string_after(text, pos);
                if (map_value.has_value()) {
                    values[map_key] = *map_value;

                    std::size_t value_start = colon + 1;
                    while (value_start < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[value_start]))) {
                        ++value_start;
                    }
                    if (value_start < text.size() && text[value_start] == '"') {
                        bool value_escaped = false;
                        for (std::size_t value_end = value_start + 1; value_end < text.size(); ++value_end) {
                            const char vc = text[value_end];
                            if (value_escaped) {
                                value_escaped = false;
                            } else if (vc == '\\') {
                                value_escaped = true;
                            } else if (vc == '"') {
                                pos = value_end;
                                break;
                            }
                        }
                    }
                }
                continue;
            }
        }

        if (advance_json_string_state(string_state, ch)) {
            continue;
        }

        if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
        }
    }
    return values;
}

std::optional<int> json_find_int(const std::string& text, const std::string& key) {
    const std::optional<std::size_t> start = json_value_start_after_key(text, key);
    if (!start.has_value()) {
        return std::nullopt;
    }
    std::size_t end = *start;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    if (end == *start) {
        return std::nullopt;
    }
    return std::stoi(text.substr(*start, end - *start));
}

std::optional<std::string> json_find_number_as_string(const std::string& text, const std::string& key) {
    // Scans the full JSON number grammar: -? (0 | [1-9][0-9]*) (\.[0-9]+)?
    // ([eE][+-]?[0-9]+)? Returns the textual form so callers can handle IDs
    // that overflow int (e.g. GitLab pipeline/job IDs) without losing precision.
    const std::optional<std::size_t> start = json_value_start_after_key(text, key);
    if (!start.has_value()) {
        return std::nullopt;
    }
    std::size_t pos = *start;
    if (pos < text.size() && text[pos] == '-') {
        ++pos;
    }
    const std::size_t digits_start = pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos == digits_start) {
        return std::nullopt;
    }
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
            ++pos;
        }
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }
    return text.substr(*start, pos - *start);
}

std::vector<std::string> json_top_level_objects(const std::string& array_text) {
    std::vector<std::string> objects;
    if (array_text.size() < 2 || array_text.front() != '[') {
        return objects;
    }

    JsonStringScanState string_state;
    int depth = 0;
    std::size_t object_start = std::string::npos;
    // Repo and feed parsing only need object slices from one array. Tracking
    // object depth avoids splitting on commas or braces that belong to nested
    // values or strings.
    for (std::size_t pos = 0; pos < array_text.size(); ++pos) {
        const char ch = array_text[pos];
        if (advance_json_string_state(string_state, ch)) {
            continue;
        }

        if (ch == '{') {
            if (depth == 0) {
                object_start = pos;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(array_text.substr(object_start, pos - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

std::string json_escape_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}
