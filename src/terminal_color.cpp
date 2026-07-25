#include "yai.hpp"

bool stdout_color_enabled() {
    // no-color.org: presence of NO_COLOR (including empty) disables color.
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    return isatty(STDOUT_FILENO) != 0;
}

std::string color_green(const std::string& text) {
    if (!stdout_color_enabled()) {
        return text;
    }
    return std::string("\033[32m") + text + "\033[0m";
}
