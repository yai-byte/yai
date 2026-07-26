#include "yai.hpp"

namespace {

constexpr std::size_t kMaxFooterRows = 8;

std::size_t clamp_bar_width(std::size_t columns) {
    std::size_t bar_width = std::min<std::size_t>(30, std::max<std::size_t>(12, columns / 4));
    if (bar_width + 4 >= columns) {
        return 12;
    }
    return bar_width;
}

} // namespace

BatchTerminalUi::BatchTerminalUi(std::size_t total_tasks)
    : total_(total_tasks), tty_(isatty(STDERR_FILENO) != 0) {}

std::string BatchTerminalUi::task_prefix(std::size_t index, const std::string& target) const {
    return "[" + std::to_string(index + 1) + "/" + std::to_string(total_) + " " + target + "] ";
}

void BatchTerminalUi::clear_footer_locked() {
    if (!tty_ || footer_lines_ == 0) {
        return;
    }
    const std::size_t columns = terminal_width();
    for (std::size_t i = 0; i < footer_lines_; ++i) {
        // Move up one line, then clear with the same \r + spaces + \r pattern as
        // clear_download_progress (CUU is required for multi-line sticky footers).
        std::cerr << "\033[A\r" << std::string(columns, ' ') << '\r';
    }
    // Drop any leftover blank rows when the footer shrinks (cursor is on the
    // first cleared line; erase from here to the end of the screen).
    std::cerr << "\033[J" << std::flush;
    footer_lines_ = 0;
}

std::string BatchTerminalUi::format_footer_row(std::size_t index, const ProgressRow& row) const {
    const std::string prefix = task_prefix(index, row.target);
    const std::size_t columns = terminal_width();
    const std::size_t prefix_width = display_width(prefix);
    const std::size_t remain = columns > prefix_width ? columns - prefix_width : 0;
    const std::size_t bar_width = clamp_bar_width(std::max<std::size_t>(remain, 16));
    const std::string bar = progress_bar(row.event.total, row.event.done, bar_width, 0);

    std::ostringstream stats;
    stats << ' ' << bar << "  ";
    if (row.event.total.has_value() && *row.event.total > 0) {
        const double ratio = std::min(
            1.0,
            static_cast<double>(row.event.done) / static_cast<double>(*row.event.total));
        stats << static_cast<int>(ratio * 100.0 + 0.5) << "%  "
              << format_byte_count(row.event.done) << '/'
              << format_byte_count(*row.event.total) << "  ";
    } else {
        stats << format_byte_count(row.event.done) << "/-  ";
    }
    stats << format_byte_count(static_cast<std::uintmax_t>(std::max(0.0, row.event.rate_bps)))
          << "/s";

    std::string line = prefix + stats.str();
    if (display_width(line) > columns) {
        line = truncate_display_width(line, columns);
    }
    return line;
}

void BatchTerminalUi::redraw_footer_locked() {
    if (!tty_) {
        return;
    }
    clear_footer_locked();

    std::size_t drawn = 0;
    for (const auto& entry : active_) {
        if (drawn >= kMaxFooterRows) {
            break;
        }
        std::cerr << format_footer_row(entry.first, entry.second) << '\n';
        ++drawn;
    }
    std::cerr << std::flush;
    footer_lines_ = drawn;
}

void BatchTerminalUi::log_parent(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_footer_locked();
    std::cerr << message << '\n';
    redraw_footer_locked();
}

void BatchTerminalUi::log_line(
    std::size_t index,
    const std::string& target,
    const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_footer_locked();
    std::cerr << task_prefix(index, target) << message << '\n';
    redraw_footer_locked();
}

void BatchTerminalUi::apply_event(
    std::size_t index,
    const std::string& target,
    const BatchProgressEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.kind == BatchProgressEvent::Kind::Clear) {
        active_.erase(index);
    } else {
        ProgressRow row;
        row.target = target;
        row.event = event;
        active_[index] = std::move(row);
    }
    if (tty_) {
        redraw_footer_locked();
    }
}

void BatchTerminalUi::clear_task(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.erase(index) == 0) {
        return;
    }
    if (tty_) {
        redraw_footer_locked();
    }
}

void BatchTerminalUi::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_footer_locked();
    active_.clear();
}
