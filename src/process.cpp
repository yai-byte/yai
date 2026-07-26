#include "yai.hpp"

#include <poll.h>

// Child-process execution and captured downloads (including progress-aware capture).

namespace {
std::vector<char*> process_argv(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void close_fd_required(int fd, const std::string& context) {
    if (close(fd) != 0) {
        throw std::runtime_error(context + tr(": close failed: ") + std::strerror(errno));
    }
}

void close_fd_best_effort(int fd) {
    if (fd >= 0) {
        // close() may report late I/O errors, but these cleanup paths are only
        // trying to release a descriptor after another failure or process exit.
        // Do not retry on EINTR because the fd state is unspecified.
        (void)close(fd);
    }
}

pid_t waitpid_nointr(pid_t pid, int* status, int options) {
    pid_t result;
    do {
        result = waitpid(pid, status, options);
    } while (result < 0 && errno == EINTR);
    return result;
}

void waitpid_required(pid_t pid, int* status, int options, const std::string& context) {
    if (waitpid_nointr(pid, status, options) < 0) {
        throw std::runtime_error(context + tr(": waitpid failed: ") + std::strerror(errno));
    }
}

void signal_process_best_effort(pid_t pid, int signal) {
    while (kill(pid, signal) != 0 && errno == EINTR) {
    }
}

void reap_process_best_effort(pid_t pid) {
    (void)waitpid_nointr(pid, nullptr, 0);
}

void exec_child_process(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& env) {
    std::vector<char*> argv = process_argv(args);
    if (cwd.has_value() && chdir(cwd->c_str()) != 0) {
        std::_Exit(126);
    }
    for (const auto& item : env) {
        if (setenv(item.first.c_str(), item.second.c_str(), 1) != 0) {
            std::_Exit(126);
        }
    }
    execvp(argv[0], argv.data());
    std::_Exit(127);
}

pid_t start_captured_process(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& env,
    int pipefd[2]) {
    // Capture stdout and stderr through one pipe so probe/download callers can
    // return useful diagnostics without leaking subprocess chatter onto yai's
    // parseable stdout. The child gets its own process group so timeout cleanup
    // can reach descendants as well as the direct process.
    if (pipe(pipefd) != 0) {
        throw std::runtime_error(std::string(tr("pipe failed: ")) + std::strerror(errno));
    }

    const pid_t pid = fork();
    if (pid < 0) {
        const int fork_errno = errno;
        close_fd_best_effort(pipefd[0]);
        close_fd_best_effort(pipefd[1]);
        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(fork_errno));
    }

    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            std::_Exit(126);
        }
        if (close(pipefd[0]) != 0) {
            std::_Exit(126);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0 ||
            close(pipefd[1]) != 0) {
            std::_Exit(126);
        }
        exec_child_process(args, cwd, env);
    }

    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
        const int setpgid_errno = errno;
        close_fd_best_effort(pipefd[0]);
        close_fd_best_effort(pipefd[1]);
        signal_process_best_effort(pid, SIGKILL);
        reap_process_best_effort(pid);
        throw std::runtime_error(std::string(tr("setpgid failed: ")) + std::strerror(setpgid_errno));
    }
    if (close(pipefd[1]) != 0) {
        const int close_errno = errno;
        close_fd_best_effort(pipefd[0]);
        signal_process_best_effort(-pid, SIGKILL);
        signal_process_best_effort(pid, SIGKILL);
        reap_process_best_effort(pid);
        throw std::runtime_error(
            std::string(tr("captured process setup: close failed: ")) + std::strerror(close_errno));
    }
    return pid;
}

void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error(std::string(tr("fcntl F_GETFL failed: ")) + std::strerror(errno));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        throw std::runtime_error(std::string(tr("fcntl F_SETFL failed: ")) + std::strerror(errno));
    }
}

void kill_and_reap(pid_t pid) {
    signal_process_best_effort(-pid, SIGKILL);
    signal_process_best_effort(pid, SIGKILL);
    reap_process_best_effort(pid);
}

enum class ReadOutputResult {
    Data,
    End,
    RetryLater,
    LimitExceeded,
};

ReadOutputResult read_output_chunk(
    int fd,
    std::string& output,
    bool nonblocking,
    const char* buffer,
    ssize_t count,
    std::size_t max_output_bytes) {
    if (count > 0) {
        const std::size_t chunk_size = static_cast<std::size_t>(count);
        if (max_output_bytes > 0 && chunk_size > max_output_bytes - output.size()) {
            output.append(buffer, max_output_bytes - output.size());
            return ReadOutputResult::LimitExceeded;
        }
        output.append(buffer, chunk_size);
        return ReadOutputResult::Data;
    }
    if (count == 0) {
        return ReadOutputResult::End;
    }
    if (errno == EINTR || (nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK))) {
        return ReadOutputResult::RetryLater;
    }
    close_fd_best_effort(fd);
    throw std::runtime_error(std::string(tr("read failed: ")) + std::strerror(errno));
}

bool append_output_until_stop(
    int fd,
    std::string& output,
    bool nonblocking,
    std::size_t max_output_bytes = 0) {
    char buffer[4096];
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        const ReadOutputResult result =
            read_output_chunk(fd, output, nonblocking, buffer, count, max_output_bytes);
        if (result == ReadOutputResult::LimitExceeded) {
            return true;
        }
        if (result == ReadOutputResult::RetryLater && !nonblocking) {
            continue;
        }
        if (result != ReadOutputResult::Data) {
            return false;
        }
    }
}

bool append_available_output(
    int fd,
    std::string& output,
    pid_t pid,
    std::size_t max_output_bytes = 0) {
    try {
        return append_output_until_stop(fd, output, true, max_output_bytes);
    } catch (const std::exception&) {
        kill_and_reap(pid);
        throw;
    }
}

void append_blocking_output(int fd, std::string& output) {
    (void)append_output_until_stop(fd, output, false);
}

void terminate_for_timeout(pid_t pid, int& status) {
    signal_process_best_effort(-pid, SIGTERM);
    signal_process_best_effort(pid, SIGTERM);
    usleep(100 * 1000);
    const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
    if (wait_result == 0) {
        signal_process_best_effort(-pid, SIGKILL);
        signal_process_best_effort(pid, SIGKILL);
        waitpid_required(pid, &status, 0, tr("timed-out process cleanup"));
    } else if (wait_result == pid) {
        signal_process_best_effort(-pid, SIGKILL);
    } else {
        signal_process_best_effort(-pid, SIGKILL);
        signal_process_best_effort(pid, SIGKILL);
    }
}

constexpr std::size_t kBatchStreamMaxLineBytes = 64 * 1024;

void strip_trailing_cr(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

enum class BatchStreamChannel {
    Log,
    Event,
};

void flush_batch_stream_line(
    BatchStreamChannel channel,
    std::string line,
    std::size_t index,
    const std::string& target,
    BatchTerminalUi& ui) {
    if (channel == BatchStreamChannel::Event) {
        const auto event = parse_batch_progress_event(line);
        if (event.has_value()) {
            ui.apply_event(index, target, *event);
        }
        return;
    }
    strip_trailing_cr(line);
    ui.log_line(index, target, line);
}

void dispatch_batch_stream_buffer(
    std::string& buffer,
    BatchStreamChannel channel,
    std::size_t index,
    const std::string& target,
    BatchTerminalUi& ui,
    bool flush_remainder) {
    while (true) {
        const std::size_t newline = buffer.find('\n');
        if (newline == std::string::npos) {
            if (flush_remainder && !buffer.empty()) {
                flush_batch_stream_line(channel, std::move(buffer), index, target, ui);
                buffer.clear();
            } else if (!flush_remainder && buffer.size() >= kBatchStreamMaxLineBytes) {
                flush_batch_stream_line(channel, buffer.substr(0, kBatchStreamMaxLineBytes), index, target, ui);
                buffer.erase(0, kBatchStreamMaxLineBytes);
                continue;
            }
            break;
        }
        flush_batch_stream_line(
            channel, buffer.substr(0, newline), index, target, ui);
        buffer.erase(0, newline + 1);
    }
}

bool read_batch_stream_fd(
    int fd,
    std::string& buffer,
    BatchStreamChannel channel,
    std::size_t index,
    const std::string& target,
    BatchTerminalUi& ui) {
    char chunk[4096];
    while (true) {
        const ssize_t count = read(fd, chunk, sizeof(chunk));
        if (count > 0) {
            buffer.append(chunk, static_cast<std::size_t>(count));
            dispatch_batch_stream_buffer(buffer, channel, index, target, ui, false);
            continue;
        }
        if (count == 0) {
            dispatch_batch_stream_buffer(buffer, channel, index, target, ui, true);
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        throw std::runtime_error(std::string(tr("read failed: ")) + std::strerror(errno));
    }
}
} // namespace

int run_process(const std::vector<std::string>& args) {
    if (args.empty()) {
        return 1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(errno));
    }
    if (pid == 0) {
        exec_child_process(args, std::nullopt, {});
    }

    int status = 0;
    waitpid_required(pid, &status, 0, tr("process"));
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128;
}

ProcessResult run_process_capture(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& env) {
    if (args.empty()) {
        return ProcessResult{1, ""};
    }

    int pipefd[2];
    const pid_t pid = start_captured_process(args, cwd, env, pipefd);
    std::string output;
    append_blocking_output(pipefd[0], output);
    close_fd_required(pipefd[0], tr("captured process output"));

    int status = 0;
    waitpid_required(pid, &status, 0, tr("captured process"));
    if (WIFEXITED(status)) {
        return ProcessResult{WEXITSTATUS(status), output};
    }
    return ProcessResult{128, output};
}

ProcessOutput run_process_capture_separate(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& env) {
    if (args.empty()) {
        return ProcessOutput{1, "", ""};
    }

    static std::atomic<unsigned long> capture_sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const unsigned long sequence = capture_sequence.fetch_add(1);
    const fs::path capture_dir =
        fs::temp_directory_path() /
        ("yai-capture-" + std::to_string(getpid()) + "-" +
         std::to_string(now) + "-" + std::to_string(sequence));
    const fs::path stdout_path = capture_dir / "stdout";
    const fs::path stderr_path = capture_dir / "stderr";
    ensure_directory(capture_dir);

    const pid_t pid = fork();
    if (pid < 0) {
        const int fork_errno = errno;
        remove_all_best_effort(capture_dir);
        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(fork_errno));
    }

    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            std::_Exit(126);
        }
        const int stdout_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int stderr_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (stdout_fd < 0 || stderr_fd < 0 ||
            dup2(stdout_fd, STDOUT_FILENO) < 0 ||
            dup2(stderr_fd, STDERR_FILENO) < 0) {
            std::_Exit(126);
        }
        close_fd_best_effort(stdout_fd);
        close_fd_best_effort(stderr_fd);
        exec_child_process(args, cwd, env);
    }

    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
        const int setpgid_errno = errno;
        signal_process_best_effort(pid, SIGKILL);
        reap_process_best_effort(pid);
        remove_all_best_effort(capture_dir);
        throw std::runtime_error(std::string(tr("setpgid failed: ")) + std::strerror(setpgid_errno));
    }

    int status = 0;
    try {
        waitpid_required(pid, &status, 0, tr("captured process"));
    } catch (const std::exception&) {
        signal_process_best_effort(-pid, SIGKILL);
        signal_process_best_effort(pid, SIGKILL);
        remove_all_best_effort(capture_dir);
        throw;
    }

    const std::string stdout_text = fs::exists(stdout_path) ? read_text_file(stdout_path) : "";
    const std::string stderr_text = fs::exists(stderr_path) ? read_text_file(stderr_path) : "";
    remove_all_best_effort(capture_dir);

    if (WIFEXITED(status)) {
        return ProcessOutput{WEXITSTATUS(status), stdout_text, stderr_text};
    }
    return ProcessOutput{128, stdout_text, stderr_text};
}

ProcessResult run_process_capture_timeout(
    const std::vector<std::string>& args,
    int timeout_ms,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& env,
    std::size_t max_output_bytes) {
    if (args.empty()) {
        return ProcessResult{1, ""};
    }

    int pipefd[2];
    const pid_t pid = start_captured_process(args, cwd, env, pipefd);
    try {
        set_nonblocking(pipefd[0]);
    } catch (const std::exception&) {
        close_fd_best_effort(pipefd[0]);
        kill_and_reap(pid);
        throw;
    }

    std::string output;
    bool timed_out = false;
    int status = 0;
    bool exited = false;
    int elapsed_ms = 0;
    constexpr int step_ms = 50;

    // Nonblocking reads keep a GUI AppImage probe from hanging forever: collect
    // whatever output exists, poll waitpid, and only then enforce the timeout.
    while (true) {
        if (append_available_output(pipefd[0], output, pid, max_output_bytes)) {
            close_fd_best_effort(pipefd[0]);
            kill_and_reap(pid);
            return ProcessResult{128, output, false, true};
        }

        const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
        if (wait_result == pid) {
            exited = true;
            break;
        }
        if (wait_result < 0) {
            close_fd_best_effort(pipefd[0]);
            throw std::runtime_error(std::string(tr("captured process poll: waitpid failed: ")) + std::strerror(errno));
        }
        if (elapsed_ms >= timeout_ms) {
            timed_out = true;
            terminate_for_timeout(pid, status);
            break;
        }
        usleep(step_ms * 1000);
        elapsed_ms += step_ms;
    }

    if (append_available_output(pipefd[0], output, pid, max_output_bytes)) {
        close_fd_best_effort(pipefd[0]);
        return ProcessResult{128, output, timed_out, true};
    }
    close_fd_required(pipefd[0], tr("captured process output"));

    if (timed_out) {
        return ProcessResult{0, output, true};
    }
    if (exited && WIFEXITED(status)) {
        return ProcessResult{WEXITSTATUS(status), output, false};
    }
    return ProcessResult{128, output, false};
}

ProcessResult run_process_capture_download_progress(
    const std::vector<std::string>& args,
    const fs::path& part,
    const fs::path& headers,
    std::optional<std::uint16_t> aria2_rpc_port) {
    if (args.empty()) {
        return ProcessResult{1, ""};
    }

    int pipefd[2];
    const pid_t pid = start_captured_process(args, std::nullopt, {}, pipefd);
    try {
        set_nonblocking(pipefd[0]);
    } catch (const std::exception&) {
        close_fd_best_effort(pipefd[0]);
        kill_and_reap(pid);
        throw;
    }

    std::string output;
    std::size_t last_width = 0;
    DownloadProgressState progress_state;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int tick = 0;
    int status = 0;
    bool exited = false;
    while (!exited) {
        // curl output remains captured for errors. yai renders its own progress
        // from aria2 RPC when a port is set, otherwise from the growing .part file
        // and dumped headers, so stdout stays owned by the command result.
        try {
            append_available_output(pipefd[0], output, pid);
        } catch (const std::exception&) {
            clear_download_progress(last_width);
            throw;
        }

        const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
        if (wait_result == pid) {
            exited = true;
            break;
        }
        if (wait_result < 0) {
            close_fd_best_effort(pipefd[0]);
            clear_download_progress(last_width);
            throw std::runtime_error(std::string(tr("download process poll: waitpid failed: ")) + std::strerror(errno));
        }
        render_download_progress(part, headers, start, tick, last_width, progress_state, aria2_rpc_port);
        ++tick;
        usleep(200 * 1000);
    }

    append_available_output(pipefd[0], output, pid);
    close_fd_required(pipefd[0], tr("download process output"));
    clear_download_progress(last_width);

    if (WIFEXITED(status)) {
        return ProcessResult{WEXITSTATUS(status), output};
    }
    return ProcessResult{128, output};
}

StreamingBatchResult run_batch_task_streaming(
    const std::vector<std::string>& args,
    const std::optional<fs::path>& cwd,
    const std::vector<std::pair<std::string, std::string>>& base_env,
    std::size_t index,
    std::size_t total,
    const std::string& target,
    BatchTerminalUi& ui) {
    (void)total;
    if (args.empty()) {
        ui.clear_task(index);
        return StreamingBatchResult{1};
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int event_pipe[2] = {-1, -1};

    auto close_pipes_best_effort = [&]() {
        close_fd_best_effort(stdout_pipe[0]);
        close_fd_best_effort(stdout_pipe[1]);
        close_fd_best_effort(stderr_pipe[0]);
        close_fd_best_effort(stderr_pipe[1]);
        close_fd_best_effort(event_pipe[0]);
        close_fd_best_effort(event_pipe[1]);
        stdout_pipe[0] = stdout_pipe[1] = -1;
        stderr_pipe[0] = stderr_pipe[1] = -1;
        event_pipe[0] = event_pipe[1] = -1;
    };

    if (pipe(stdout_pipe) != 0) {
        throw std::runtime_error(std::string(tr("pipe failed: ")) + std::strerror(errno));
    }
    if (pipe(stderr_pipe) != 0) {
        const int pipe_errno = errno;
        close_pipes_best_effort();
        throw std::runtime_error(std::string(tr("pipe failed: ")) + std::strerror(pipe_errno));
    }
    if (pipe(event_pipe) != 0) {
        const int pipe_errno = errno;
        close_pipes_best_effort();
        throw std::runtime_error(std::string(tr("pipe failed: ")) + std::strerror(pipe_errno));
    }

    const pid_t pid = fork();
    if (pid < 0) {
        const int fork_errno = errno;
        close_pipes_best_effort();
        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(fork_errno));
    }

    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            std::_Exit(126);
        }
        if (close(stdout_pipe[0]) != 0 || close(stderr_pipe[0]) != 0 || close(event_pipe[0]) != 0) {
            std::_Exit(126);
        }
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            std::_Exit(126);
        }
        if (stdout_pipe[1] != STDOUT_FILENO && close(stdout_pipe[1]) != 0) {
            std::_Exit(126);
        }
        if (stderr_pipe[1] != STDERR_FILENO && close(stderr_pipe[1]) != 0) {
            std::_Exit(126);
        }

        std::vector<std::pair<std::string, std::string>> env = base_env;
        env.emplace_back("YAI_BATCH_CHILD", "1");
        env.emplace_back("YAI_BATCH_EVENT_FD", std::to_string(event_pipe[1]));
        exec_child_process(args, cwd, env);
    }

    try {
        if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
            const int setpgid_errno = errno;
            close_pipes_best_effort();
            signal_process_best_effort(pid, SIGKILL);
            reap_process_best_effort(pid);
            ui.clear_task(index);
            throw std::runtime_error(std::string(tr("setpgid failed: ")) + std::strerror(setpgid_errno));
        }

        close_fd_required(stdout_pipe[1], tr("batch stream stdout write"));
        stdout_pipe[1] = -1;
        close_fd_required(stderr_pipe[1], tr("batch stream stderr write"));
        stderr_pipe[1] = -1;
        close_fd_required(event_pipe[1], tr("batch stream event write"));
        event_pipe[1] = -1;

        set_nonblocking(stdout_pipe[0]);
        set_nonblocking(stderr_pipe[0]);
        set_nonblocking(event_pipe[0]);

        std::string stdout_buf;
        std::string stderr_buf;
        std::string event_buf;
        bool stdout_open = true;
        bool stderr_open = true;
        bool event_open = true;
        bool exited = false;
        int status = 0;

        while (stdout_open || stderr_open || event_open || !exited) {
            if (!exited) {
                const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
                if (wait_result == pid) {
                    exited = true;
                } else if (wait_result < 0) {
                    throw std::runtime_error(
                        std::string(tr("batch stream: waitpid failed: ")) + std::strerror(errno));
                }
            }

            pollfd pfds[3];
            nfds_t nfds = 0;
            int stdout_slot = -1;
            int stderr_slot = -1;
            int event_slot = -1;
            if (stdout_open) {
                stdout_slot = static_cast<int>(nfds);
                pfds[nfds++] = pollfd{stdout_pipe[0], POLLIN, 0};
            }
            if (stderr_open) {
                stderr_slot = static_cast<int>(nfds);
                pfds[nfds++] = pollfd{stderr_pipe[0], POLLIN, 0};
            }
            if (event_open) {
                event_slot = static_cast<int>(nfds);
                pfds[nfds++] = pollfd{event_pipe[0], POLLIN, 0};
            }

            const int timeout_ms = (nfds == 0) ? 0 : (exited ? 0 : 200);
            if (nfds > 0) {
                const int ready = poll(pfds, nfds, timeout_ms);
                if (ready < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    throw std::runtime_error(
                        std::string(tr("batch stream: poll failed: ")) + std::strerror(errno));
                }
            } else if (!exited) {
                usleep(50 * 1000);
                continue;
            } else {
                break;
            }

            auto handle_slot = [&](int slot, int& fd, bool& open_flag, std::string& buffer,
                                   BatchStreamChannel channel) {
                if (slot < 0 || !open_flag) {
                    return;
                }
                const short revents = pfds[slot].revents;
                if ((revents & (POLLERR | POLLNVAL)) != 0) {
                    dispatch_batch_stream_buffer(buffer, channel, index, target, ui, true);
                    close_fd_best_effort(fd);
                    fd = -1;
                    open_flag = false;
                    return;
                }
                if ((revents & (POLLIN | POLLHUP)) == 0) {
                    return;
                }
                if (read_batch_stream_fd(fd, buffer, channel, index, target, ui)) {
                    close_fd_best_effort(fd);
                    fd = -1;
                    open_flag = false;
                }
            };

            handle_slot(stdout_slot, stdout_pipe[0], stdout_open, stdout_buf, BatchStreamChannel::Log);
            handle_slot(stderr_slot, stderr_pipe[0], stderr_open, stderr_buf, BatchStreamChannel::Log);
            handle_slot(event_slot, event_pipe[0], event_open, event_buf, BatchStreamChannel::Event);
        }

        if (!exited) {
            waitpid_required(pid, &status, 0, tr("batch stream"));
            exited = true;
        }

        close_fd_best_effort(stdout_pipe[0]);
        close_fd_best_effort(stderr_pipe[0]);
        close_fd_best_effort(event_pipe[0]);
        stdout_pipe[0] = stderr_pipe[0] = event_pipe[0] = -1;

        ui.clear_task(index);
        if (WIFEXITED(status)) {
            return StreamingBatchResult{WEXITSTATUS(status)};
        }
        return StreamingBatchResult{128};
    } catch (...) {
        kill_and_reap(pid);
        close_pipes_best_effort();
        ui.clear_task(index);
        throw;
    }
}
