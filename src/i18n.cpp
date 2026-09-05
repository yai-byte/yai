#include "yai.hpp"

#include <unordered_map>

// Locale selection and gettext-style po catalog loading for tr()/tr_format().

namespace {

std::optional<std::string> po_unquote(const std::string& line, std::size_t start) {
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    if (start >= line.size() || line[start] != '"') {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t i = start + 1; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            return value;
        }
        if (ch != '\\' || i + 1 >= line.size()) {
            value.push_back(ch);
            continue;
        }

        const char escaped = line[++i];
        if (escaped == 'n') {
            value.push_back('\n');
        } else if (escaped == 't') {
            value.push_back('\t');
        } else if (escaped == 'r') {
            value.push_back('\r');
        } else {
            value.push_back(escaped);
        }
    }
    return std::nullopt;
}

std::optional<fs::path> executable_dir_path() {
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    buffer[length] = '\0';
    return fs::path(buffer).parent_path();
}

std::vector<fs::path> translation_dirs() {
    std::vector<fs::path> dirs;
    if (const std::optional<fs::path> exe_dir = executable_dir_path()) {
        dirs.push_back(*exe_dir / "po");
        dirs.push_back(*exe_dir / ".." / "share" / "yai" / "po");
    }
    dirs.push_back(fs::current_path() / "po");
    return dirs;
}

std::string language_po_file(Language language) {
    return language == Language::Chinese ? "zh.po" : "en.po";
}

std::unordered_map<std::string, std::string> parse_po_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    enum class Field {
        None,
        Msgid,
        Msgstr,
    };

    std::unordered_map<std::string, std::string> catalog;
    std::string msgid;
    std::string msgstr;
    bool have_msgid = false;
    bool have_msgstr = false;
    Field field = Field::None;

    auto flush = [&]() {
        if (have_msgid && have_msgstr && !msgid.empty()) {
            catalog[msgid] = msgstr;
        }
        msgid.clear();
        msgstr.clear();
        have_msgid = false;
        have_msgstr = false;
        field = Field::None;
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#') {
            if (stripped.empty()) {
                flush();
            }
            continue;
        }

        if (stripped.rfind("msgid", 0) == 0) {
            flush();
            if (const std::optional<std::string> value = po_unquote(stripped, 5)) {
                msgid = *value;
                have_msgid = true;
                field = Field::Msgid;
            }
            continue;
        }
        if (stripped.rfind("msgstr", 0) == 0) {
            if (const std::optional<std::string> value = po_unquote(stripped, 6)) {
                msgstr = *value;
                have_msgstr = true;
                field = Field::Msgstr;
            }
            continue;
        }
        if (stripped[0] == '"') {
            const std::optional<std::string> value = po_unquote(stripped, 0);
            if (!value.has_value()) {
                continue;
            }
            if (field == Field::Msgid) {
                msgid += *value;
            } else if (field == Field::Msgstr) {
                msgstr += *value;
            }
        }
    }
    flush();
    return catalog;
}

std::unordered_map<std::string, std::string> load_translation_catalog(Language language) {
    const std::string filename = language_po_file(language);
    for (const fs::path& dir : translation_dirs()) {
        std::error_code ec;
        const fs::path file = fs::weakly_canonical(dir / filename, ec);
        const fs::path candidate = ec ? dir / filename : file;
        if (fs::exists(candidate)) {
            return parse_po_file(candidate);
        }
    }
    return {};
}

const std::unordered_map<std::string, std::string>& translation_catalog(Language language) {
    static const std::unordered_map<std::string, std::string> english =
        load_translation_catalog(Language::English);
    static const std::unordered_map<std::string, std::string> chinese =
        load_translation_catalog(Language::Chinese);
    return language == Language::Chinese ? chinese : english;
}

} // namespace

Language current_language() {
    const std::optional<std::string> explicit_lang = env_string("YAI_LANG");
    if (explicit_lang.has_value()) {
        const std::string requested = ascii_lower(*explicit_lang);
        if (requested.rfind("zh", 0) == 0) {
            return Language::Chinese;
        }
        if (requested.rfind("en", 0) == 0 || requested == "c" || requested == "posix") {
            return Language::English;
        }
    }

    const std::vector<const char*> locale_names = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : locale_names) {
        const std::optional<std::string> value = env_string(name);
        if (!value.has_value()) {
            continue;
        }
        const std::string lower = ascii_lower(*value);
        if (lower.rfind("zh", 0) == 0 || lower.find(".zh") != std::string::npos) {
            return Language::Chinese;
        }
        if (lower.rfind("en", 0) == 0 || lower == "c" || lower == "posix") {
            return Language::English;
        }
    }

    return Language::English;
}

std::string tr(const std::string& msgid) {
    const auto& catalog = translation_catalog(current_language());
    const auto translated = catalog.find(msgid);
    if (translated != catalog.end() && !translated->second.empty()) {
        return translated->second;
    }
    return msgid;
}

std::string tr_format(
    const std::string& msgid,
    const std::vector<std::pair<std::string, std::string>>& replacements) {
    std::string value = tr(msgid);
    for (const auto& replacement : replacements) {
        value = replace_all(value, replacement.first, replacement.second);
    }
    return value;
}
