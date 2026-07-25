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
        } else {
            out.push_back(json_simple_escape(next));
        }
    }
    return out;
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
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    return json_string_after(text, key_pos);
}

std::vector<std::string> json_find_all_strings(const std::string& text, const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
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
    const std::size_t key_pos = text.find(needle);
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
