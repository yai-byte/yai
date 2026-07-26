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

namespace {

std::optional<std::uint16_t> read_big_endian_u16(
    const std::vector<unsigned char>& bytes,
    std::size_t& offset) {
    if (offset + 2 > bytes.size()) {
        return std::nullopt;
    }
    const std::uint16_t value =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) |
                                   static_cast<std::uint16_t>(bytes[offset + 1]));
    offset += 2;
    return value;
}

std::optional<std::uint32_t> read_big_endian_u32(
    const std::vector<unsigned char>& bytes,
    std::size_t& offset) {
    if (offset + 4 > bytes.size()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<std::uint32_t>(bytes[offset + i]);
    }
    offset += 4;
    return value;
}

std::optional<std::uint64_t> read_big_endian_u64(
    const std::vector<unsigned char>& bytes,
    std::size_t& offset) {
    if (offset + 8 > bytes.size()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(bytes[offset + i]);
    }
    offset += 8;
    return value;
}

bool consume_bytes(std::size_t count, const std::vector<unsigned char>& bytes, std::size_t& offset) {
    if (offset + count > bytes.size()) {
        return false;
    }
    offset += count;
    return true;
}

bool bitfield_bit_is_set(const std::vector<unsigned char>& bitfield, std::uint64_t bit_index) {
    const std::uint64_t byte_index = bit_index / 8;
    if (byte_index >= bitfield.size()) {
        return false;
    }
    const unsigned char mask = static_cast<unsigned char>(0x80u >> (bit_index % 8));
    return (bitfield[static_cast<std::size_t>(byte_index)] & mask) != 0;
}

std::uint64_t bytes_for_range_chunk(std::uint64_t length, std::uint64_t offset, std::uint64_t chunk_size) {
    if (offset >= length) {
        return 0;
    }
    return std::min(chunk_size, length - offset);
}

std::optional<std::uintmax_t> aria2_control_downloaded_bytes(const fs::path& control) {
    std::ifstream in(control, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    std::size_t offset = 0;

    const std::optional<std::uint16_t> version = read_big_endian_u16(bytes, offset);
    if (!version.has_value() || *version != 1) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> extension_length = read_big_endian_u32(bytes, offset);
    if (!extension_length.has_value() || !consume_bytes(*extension_length, bytes, offset)) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> info_hash_length = read_big_endian_u32(bytes, offset);
    if (!info_hash_length.has_value() || !consume_bytes(*info_hash_length, bytes, offset)) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> piece_length = read_big_endian_u32(bytes, offset);
    const std::optional<std::uint64_t> total_length = read_big_endian_u64(bytes, offset);
    const std::optional<std::uint64_t> upload_length = read_big_endian_u64(bytes, offset);
    (void)upload_length;
    if (!piece_length.has_value() || *piece_length == 0 || !total_length.has_value()) {
        return std::nullopt;
    }

    const std::optional<std::uint32_t> bitfield_length = read_big_endian_u32(bytes, offset);
    if (!bitfield_length.has_value() || offset + *bitfield_length > bytes.size()) {
        return std::nullopt;
    }
    const std::vector<unsigned char> completed_bitfield(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + *bitfield_length));
    offset += *bitfield_length;

    const std::uint64_t piece_count = (*total_length + *piece_length - 1) / *piece_length;
    std::uint64_t downloaded = 0;
    for (std::uint64_t i = 0; i < piece_count; ++i) {
        if (bitfield_bit_is_set(completed_bitfield, i)) {
            downloaded += bytes_for_range_chunk(*total_length, i * *piece_length, *piece_length);
        }
    }

    const std::optional<std::uint32_t> inflight_count = read_big_endian_u32(bytes, offset);
    if (!inflight_count.has_value()) {
        return std::min<std::uint64_t>(downloaded, *total_length);
    }

    constexpr std::uint64_t aria2_inflight_chunk_size = 16 * 1024;
    for (std::uint32_t i = 0; i < *inflight_count; ++i) {
        const std::optional<std::uint32_t> piece_index = read_big_endian_u32(bytes, offset);
        const std::optional<std::uint32_t> length = read_big_endian_u32(bytes, offset);
        const std::optional<std::uint32_t> piece_bitfield_length = read_big_endian_u32(bytes, offset);
        if (!piece_index.has_value() || !length.has_value() || !piece_bitfield_length.has_value() ||
            offset + *piece_bitfield_length > bytes.size()) {
            return std::nullopt;
        }
        const std::vector<unsigned char> piece_bitfield(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + *piece_bitfield_length));
        offset += *piece_bitfield_length;

        if (*piece_index >= piece_count || bitfield_bit_is_set(completed_bitfield, *piece_index)) {
            continue;
        }
        const std::uint64_t piece_start = static_cast<std::uint64_t>(*piece_index) * *piece_length;
        const std::uint64_t piece_bytes = bytes_for_range_chunk(*total_length, piece_start, *length);
        const std::uint64_t chunk_count =
            (piece_bytes + aria2_inflight_chunk_size - 1) / aria2_inflight_chunk_size;
        for (std::uint64_t chunk = 0; chunk < chunk_count; ++chunk) {
            if (bitfield_bit_is_set(piece_bitfield, chunk)) {
                downloaded += bytes_for_range_chunk(
                    piece_bytes,
                    chunk * aria2_inflight_chunk_size,
                    aria2_inflight_chunk_size);
            }
        }
    }

    return static_cast<std::uintmax_t>(std::min<std::uint64_t>(downloaded, *total_length));
}

} // namespace

std::uintmax_t download_progress_downloaded_bytes(const fs::path& part) {
    const std::optional<std::uintmax_t> aria2_downloaded =
        aria2_control_downloaded_bytes(part.string() + ".aria2");
    if (aria2_downloaded.has_value()) {
        return *aria2_downloaded;
    }

    std::error_code ec;
    const std::uintmax_t downloaded = fs::file_size(part, ec);
    if (ec) {
        throw fs::filesystem_error("download progress file size", part, ec);
    }
    return downloaded;
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
    DownloadProgressState& state) {
    std::uintmax_t downloaded = 0;
    try {
        downloaded = download_progress_downloaded_bytes(part);
    } catch (const fs::filesystem_error&) {
        return std::nullopt;
    }

    DownloadProgressSnapshot snapshot;
    snapshot.downloaded = downloaded;
    snapshot.total = download_total_from_headers(headers);
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    snapshot.elapsed = std::max(0.001, std::chrono::duration<double>(now - start).count());
    snapshot.bytes_per_second = download_progress_recent_speed(state, now, downloaded);

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

void render_download_progress(
    const fs::path& part,
    const fs::path& headers,
    const std::chrono::steady_clock::time_point& start,
    int tick,
    std::size_t& last_width,
    DownloadProgressState& state) {
    const int event_fd = batch_event_fd();
    const std::optional<DownloadProgressSnapshot> snapshot =
        download_progress_snapshot(part, headers, start, state);
    if (!snapshot.has_value()) {
        return;
    }

    if (event_fd >= 0) {
        const std::string line = format_batch_progress_event(
            snapshot->downloaded,
            snapshot->total,
            snapshot->bytes_per_second) + "\n";
        write_event_line(event_fd, line);
        return;
    }

    // Progress is user-facing status, so it is TTY-only stderr. Redirected
    // commands keep stdout/stderr stable, and unknown totals fall back to the
    // animated bar instead of pretending a percentage is known.
    if (isatty(STDERR_FILENO) == 0) {
        return;
    }

    const std::string stats = format_download_progress_stats(*snapshot);
    write_progress_line(render_progress_line(stats, *snapshot, terminal_width(), tick), last_width);
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
