#include "yai.hpp"

// Terminal download progress rendering and aria2/curl progress probes.

std::string format_byte_count(std::uintmax_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    if (unit == 0 || value >= 100.0) {
        out << static_cast<std::uintmax_t>(value);
    } else {
        out.setf(std::ios::fixed);
        out.precision(value >= 10.0 ? 1 : 2);
        out << value;
    }
    out << ' ' << units[unit];
    return out.str();
}

std::string format_duration_seconds(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const unsigned long total = static_cast<unsigned long>(seconds + 0.5);
    const unsigned long hours = total / 3600;
    const unsigned long minutes = (total / 60) % 60;
    const unsigned long secs = total % 60;

    std::ostringstream out;
    if (hours > 0) {
        out << hours << ':';
        if (minutes < 10) {
            out << '0';
        }
    }
    out << minutes << ':';
    if (secs < 10) {
        out << '0';
    }
    out << secs;
    return out.str();
}

std::size_t display_width(const std::string& value) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch < 0x80) {
            ++width;
        } else {
            ++width;
            while (i + 1 < value.size() &&
                   (static_cast<unsigned char>(value[i + 1]) & 0xc0) == 0x80) {
                ++i;
            }
            ++width;
        }
    }
    return width;
}

std::string truncate_display_width(const std::string& value, std::size_t max_width) {
    if (display_width(value) <= max_width) {
        return value;
    }
    if (max_width <= 3) {
        return value.substr(0, std::min(value.size(), max_width));
    }

    std::string out;
    std::size_t width = 0;
    const std::size_t content_width = max_width - 3;
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        std::size_t char_bytes = 1;
        std::size_t char_width = 1;
        if (ch >= 0x80) {
            while (i + char_bytes < value.size() &&
                   (static_cast<unsigned char>(value[i + char_bytes]) & 0xc0) == 0x80) {
                ++char_bytes;
            }
            char_width = 2;
        }
        if (width + char_width > content_width) {
            break;
        }
        out.append(value, i, char_bytes);
        width += char_width;
        i += char_bytes;
    }
    out += "...";
    return out;
}

std::size_t terminal_width() {
    winsize size {};
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return 80;
}

std::optional<std::uintmax_t> download_total_from_headers(const fs::path& headers) {
    // curl writes final response headers while the body streams to .part. Reading
    // Content-Length from that same transfer avoids an extra HEAD request and
    // still supports redirects through the last observed length.
    std::ifstream in(headers);
    if (!in) {
        return std::nullopt;
    }

    std::optional<std::uintmax_t> total;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        const std::string lower = to_lower(line);
        const std::string prefix = "content-length:";
        if (lower.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string value = trim(line.substr(prefix.size()));
        if (value.empty()) {
            continue;
        }
        try {
            total = static_cast<std::uintmax_t>(std::stoull(value));
        } catch (const std::exception&) {
        }
    }
    return total;
}

std::optional<Aria2RpcProgress> parse_aria2_tell_active_response(const std::string& json) {
    // aria2 encodes lengths/speeds as JSON strings. Empty result[] means not ready.
    if (json.find("\"result\":[]") != std::string::npos) {
        return std::nullopt;
    }
    const std::optional<std::string> completed = json_find_string(json, "completedLength");
    if (!completed.has_value() || completed->empty()) {
        return std::nullopt;
    }
    Aria2RpcProgress out;
    try {
        out.completed = static_cast<std::uintmax_t>(std::stoull(*completed));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (const std::optional<std::string> total = json_find_string(json, "totalLength")) {
        try {
            const std::uintmax_t value = static_cast<std::uintmax_t>(std::stoull(*total));
            if (value > 0) {
                out.total = value;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<std::string> speed = json_find_string(json, "downloadSpeed")) {
        try {
            out.speed_bps = static_cast<double>(std::stoull(*speed));
        } catch (const std::exception&) {
        }
    }
    return out;
}

std::optional<Aria2RpcProgress> merge_aria2_rpc_progress(
    const std::optional<Aria2RpcProgress>& previous,
    const std::optional<Aria2RpcProgress>& current) {
    if (current.has_value()) {
        return current;
    }
    return previous;
}

std::optional<Aria2RpcProgress> query_aria2_rpc_progress(std::uint16_t port) {
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/jsonrpc";
    const std::string body =
        "{\"jsonrpc\":\"2.0\",\"id\":\"yai\",\"method\":\"aria2.tellActive\",\"params\":[]}";
    const ProcessResult result = run_process_capture_timeout({
        "curl",
        "--silent",
        "--show-error",
        "--max-time",
        "1",
        "--header",
        "Content-Type: application/json",
        "--data",
        body,
        url,
    }, 1500);
    if (result.exit_code != 0 || result.timed_out) {
        return std::nullopt;
    }
    return parse_aria2_tell_active_response(result.output);
}

double download_progress_recent_speed(
    DownloadProgressState& state,
    const std::chrono::steady_clock::time_point& now,
    std::uintmax_t downloaded) {
    if (!state.samples.empty() && downloaded < state.samples.back().downloaded) {
        state.samples.clear();
        state.last_progress_time = std::nullopt;
        state.bytes_per_second = 0.0;
    }

    if (!state.samples.empty() && downloaded > state.samples.back().downloaded) {
        state.last_progress_time = now;
    }
    state.samples.push_back(DownloadProgressSample{now, downloaded});
    while (state.samples.size() > 1 &&
           std::chrono::duration<double>(now - state.samples.front().time).count() > 1.0) {
        state.samples.erase(state.samples.begin());
    }

    for (const DownloadProgressSample& sample : state.samples) {
        if (sample.downloaded >= downloaded) {
            continue;
        }
        const double elapsed = std::chrono::duration<double>(now - sample.time).count();
        if (elapsed > 0.0) {
            state.bytes_per_second = static_cast<double>(downloaded - sample.downloaded) / elapsed;
            return state.bytes_per_second;
        }
    }
    if (downloaded == 0 ||
        (state.last_progress_time.has_value() &&
         std::chrono::duration<double>(now - *state.last_progress_time).count() > 1.5)) {
        state.bytes_per_second = 0.0;
    }
    return state.bytes_per_second;
}

std::string progress_bar(
    std::optional<std::uintmax_t> total,
    std::uintmax_t downloaded,
    std::size_t width,
    int tick) {
    if (width < 4) {
        return "";
    }
    const std::size_t inner = width - 2;
    std::string bar;
    bar.reserve(width);
    bar.push_back('[');

    if (total.has_value() && *total > 0) {
        const double ratio = std::min(1.0, static_cast<double>(downloaded) / static_cast<double>(*total));
        const std::size_t filled = static_cast<std::size_t>(ratio * static_cast<double>(inner) + 0.5);
        for (std::size_t i = 0; i < inner; ++i) {
            bar.push_back(i < filled ? '#' : '-');
        }
    } else {
        // Some servers do not provide Content-Length. In that case show motion
        // and byte/speed stats without inventing a completion percentage.
        const std::size_t block = std::min<std::size_t>(3, inner);
        const std::size_t range = inner > block ? inner - block : 1;
        const std::size_t offset = static_cast<std::size_t>(tick) % range;
        for (std::size_t i = 0; i < inner; ++i) {
            bar.push_back(i >= offset && i < offset + block ? '#' : '-');
        }
    }

    bar.push_back(']');
    return bar;
}

namespace {

struct DownloadProgressSnapshot {
    std::uintmax_t downloaded = 0;
    std::optional<std::uintmax_t> total;
    double elapsed = 0.0;
    double bytes_per_second = 0.0;
    double total_seconds = -1.0;
    double left_seconds = -1.0;
};

std::optional<DownloadProgressSnapshot> download_progress_snapshot(
    const fs::path& part,
    const fs::path& headers,
    const std::chrono::steady_clock::time_point& start,
    DownloadProgressState& state,
    std::optional<std::uint16_t> aria2_rpc_port) {
    std::uintmax_t downloaded = 0;
    std::optional<std::uintmax_t> total = download_total_from_headers(headers);
    double rpc_speed = -1.0;

    if (aria2_rpc_port.has_value()) {
        const auto merged = merge_aria2_rpc_progress(
            state.last_aria2_rpc,
            query_aria2_rpc_progress(*aria2_rpc_port));
        state.last_aria2_rpc = merged;
        if (merged.has_value()) {
            downloaded = merged->completed;
            if (merged->total.has_value()) {
                total = merged->total;
            }
            if (merged->speed_bps.has_value()) {
                rpc_speed = *merged->speed_bps;
            }
        }
    } else {
        // curl/wget: sequential growth — file_size is correct.
        std::error_code ec;
        downloaded = fs::file_size(part, ec);
        if (ec) {
            return std::nullopt;
        }
    }

    DownloadProgressSnapshot snapshot;
    snapshot.downloaded = downloaded;
    snapshot.total = total;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    snapshot.elapsed = std::max(0.001, std::chrono::duration<double>(now - start).count());
    if (rpc_speed >= 0) {
        snapshot.bytes_per_second = rpc_speed;
    } else {
        snapshot.bytes_per_second = download_progress_recent_speed(state, now, downloaded);
    }

    const bool knows_total = snapshot.total.has_value() && *snapshot.total > 0;
    if (knows_total) {
        snapshot.total_seconds = static_cast<double>(*snapshot.total) / std::max(1.0, snapshot.bytes_per_second);
        const std::uintmax_t bytes_left = *snapshot.total > downloaded ? *snapshot.total - downloaded : 0;
        snapshot.left_seconds = std::max(0.0, static_cast<double>(bytes_left) / std::max(1.0, snapshot.bytes_per_second));
    }
    // When total is unknown, total_seconds and left_seconds intentionally stay
    // negative; the formatter renders them as "unknown" while still reporting
    // downloaded bytes and current speed.
    return snapshot;
}

bool download_progress_knows_total(const DownloadProgressSnapshot& snapshot) {
    return snapshot.total.has_value() && *snapshot.total > 0;
}

std::string format_download_progress_stats(const DownloadProgressSnapshot& snapshot) {
    const bool knows_total = download_progress_knows_total(snapshot);
    const std::string unknown = tr("unknown");
    return
        tr("Total:") + (knows_total ? format_byte_count(*snapshot.total) : unknown) + " " +
        tr("Downloaded:") + format_byte_count(snapshot.downloaded) + " " +
        tr("Speed:") + format_byte_count(static_cast<std::uintmax_t>(snapshot.bytes_per_second)) + "/s " +
        tr("Total time:") + (knows_total ? format_duration_seconds(snapshot.total_seconds) : unknown) + " " +
        tr("Elapsed:") + format_duration_seconds(snapshot.elapsed) + " " +
        tr("Left:") + (knows_total ? format_duration_seconds(snapshot.left_seconds) : unknown);
}

std::size_t progress_bar_width(std::size_t columns) {
    std::size_t bar_width = std::min<std::size_t>(30, std::max<std::size_t>(12, columns / 4));
    if (bar_width + 4 >= columns) {
        return 12;
    }
    return bar_width;
}

std::string render_progress_line(
    const std::string& stats,
    const DownloadProgressSnapshot& snapshot,
    std::size_t columns,
    int tick) {
    if (columns < 24) {
        return truncate_display_width(stats, columns);
    }

    const std::size_t bar_width = progress_bar_width(columns);
    const std::string bar = progress_bar(snapshot.total, snapshot.downloaded, bar_width, tick);
    const std::size_t bar_display_width = display_width(bar);
    const std::size_t stats_max_width = columns > bar_display_width + 1
        ? columns - bar_display_width - 1
        : 0;
    const std::string visible_stats = truncate_display_width(stats, stats_max_width);
    const std::size_t visible_stats_width = display_width(visible_stats);

    std::string line = visible_stats;
    if (columns > visible_stats_width + bar_display_width) {
        line.append(columns - visible_stats_width - bar_display_width, ' ');
    }
    line += bar;
    return line;
}

void write_progress_line(std::string line, std::size_t& last_width) {
    const std::size_t line_width = display_width(line);
    if (line_width < last_width) {
        line.append(last_width - line_width, ' ');
    }
    last_width = std::max(last_width, line_width);
    std::cerr << '\r' << line << std::flush;
}

void write_event_line(int event_fd, const std::string& line) {
    const char* data = line.data();
    std::size_t left = line.size();
    while (left > 0) {
        const ssize_t written = write(event_fd, data, left);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        left -= static_cast<std::size_t>(written);
    }
}

} // namespace

std::string format_download_progress_line(
    std::uintmax_t downloaded,
    std::optional<std::uintmax_t> total,
    double bytes_per_second,
    double elapsed,
    double total_seconds,
    double left_seconds,
    std::size_t columns,
    int tick) {
    DownloadProgressSnapshot snapshot;
    snapshot.downloaded = downloaded;
    snapshot.total = total;
    snapshot.bytes_per_second = bytes_per_second;
    snapshot.elapsed = elapsed;
    snapshot.total_seconds = total_seconds;
    snapshot.left_seconds = left_seconds;
    const std::string stats = format_download_progress_stats(snapshot);
    return render_progress_line(stats, snapshot, columns, tick);
}

void render_download_progress(
    const fs::path& part,
    const fs::path& headers,
    const std::chrono::steady_clock::time_point& start,
    int tick,
    std::size_t& last_width,
    DownloadProgressState& state,
    std::optional<std::uint16_t> aria2_rpc_port) {
    const int event_fd = batch_event_fd();
    const std::optional<DownloadProgressSnapshot> snapshot =
        download_progress_snapshot(part, headers, start, state, aria2_rpc_port);
    if (!snapshot.has_value()) {
        return;
    }

    if (event_fd >= 0) {
        BatchProgressEvent event;
        event.kind = BatchProgressEvent::Kind::Progress;
        event.done = snapshot->downloaded;
        event.total = snapshot->total;
        event.rate_bps = snapshot->bytes_per_second;
        event.elapsed = snapshot->elapsed;
        event.total_seconds = snapshot->total_seconds;
        event.left_seconds = snapshot->left_seconds;
        write_event_line(event_fd, format_batch_progress_event(event) + "\n");
        return;
    }

    // Progress is user-facing status, so it is TTY-only stderr. Redirected
    // commands keep stdout/stderr stable, and unknown totals fall back to the
    // animated bar instead of pretending a percentage is known.
    if (isatty(STDERR_FILENO) == 0) {
        return;
    }

    write_progress_line(
        format_download_progress_line(
            snapshot->downloaded,
            snapshot->total,
            snapshot->bytes_per_second,
            snapshot->elapsed,
            snapshot->total_seconds,
            snapshot->left_seconds,
            terminal_width(),
            tick),
        last_width);
}

void clear_download_progress(std::size_t& last_width) {
    const int event_fd = batch_event_fd();
    if (event_fd >= 0) {
        const std::string line = format_batch_progress_clear_event() + "\n";
        write_event_line(event_fd, line);
        last_width = 0;
        return;
    }
    if (isatty(STDERR_FILENO) == 0 || last_width == 0) {
        return;
    }
    std::cerr << '\r' << std::string(last_width, ' ') << '\r' << std::flush;
    last_width = 0;
}
