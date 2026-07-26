#include "yai.hpp"

namespace {

constexpr std::size_t kMaxFooterRows = 8;

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
    // Match single-package progress layout exactly (Total/Downloaded/Speed/... + bar),
    // with only the batch task prefix added in front.
    const std::string prefix = task_prefix(index, row.target);
    const std::size_t columns = terminal_width();
    const std::size_t prefix_width = display_width(prefix);
    const std::size_t body_columns = columns > prefix_width ? columns - prefix_width : 0;
    return prefix + format_download_progress_line(
               row.event.done,
               row.event.total,
               row.event.rate_bps,
               row.event.elapsed,
               row.event.total_seconds,
               row.event.left_seconds,
               body_columns,
               row.tick);
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
    std::string text = message;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    clear_footer_locked();
    std::cerr << text << '\n';
    redraw_footer_locked();
}

void BatchTerminalUi::log_line(
    std::size_t index,
    const std::string& target,
    const std::string& message) {
    std::string text = message;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    clear_footer_locked();
    std::cerr << task_prefix(index, target) << text << '\n';
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
        ProgressRow& row = active_[index];
        const int next_tick = row.tick + 1;
        row.target = target;
        row.event = event;
        row.tick = next_tick;
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
