# Task 2 review package
## Stat
  447 src/process.cpp
  965 src/core.cpp
 1412 总计
## Makefile diff
--- .superpowers/sdd/Makefile.pre-task2	2026-07-24 21:57:17.509166867 +0800
+++ Makefile	2026-07-24 21:57:51.577698250 +0800
@@ -13,6 +13,7 @@
 	src/i18n.cpp \
 	src/json.cpp \
 	src/main.cpp \
+	src/process.cpp \
 	src/repo.cpp \
 	src/resolver.cpp
 
## core.cpp diff (vs pre-task2)
--- .superpowers/sdd/core.cpp.pre-task2	2026-07-24 21:57:17.507940686 +0800
+++ src/core.cpp	2026-07-24 21:57:49.552797430 +0800
@@ -267,387 +267,6 @@
            lower.find("fuse kernel module") != std::string::npos;
 }
 
-std::vector<char*> process_argv(const std::vector<std::string>& args) {
-    std::vector<char*> argv;
-    argv.reserve(args.size() + 1);
-    for (const std::string& arg : args) {
-        argv.push_back(const_cast<char*>(arg.c_str()));
-    }
-    argv.push_back(nullptr);
-    return argv;
-}
-
-void close_fd_required(int fd, const std::string& context) {
-    if (close(fd) != 0) {
-        throw std::runtime_error(context + tr(": close failed: ") + std::strerror(errno));
-    }
-}
-
-void close_fd_best_effort(int fd) {
-    if (fd >= 0) {
-        // close() may report late I/O errors, but these cleanup paths are only
-        // trying to release a descriptor after another failure or process exit.
-        // Do not retry on EINTR because the fd state is unspecified.
-        (void)close(fd);
-    }
-}
-
-pid_t waitpid_nointr(pid_t pid, int* status, int options) {
-    pid_t result;
-    do {
-        result = waitpid(pid, status, options);
-    } while (result < 0 && errno == EINTR);
-    return result;
-}
-
-void waitpid_required(pid_t pid, int* status, int options, const std::string& context) {
-    if (waitpid_nointr(pid, status, options) < 0) {
-        throw std::runtime_error(context + tr(": waitpid failed: ") + std::strerror(errno));
-    }
-}
-
-void signal_process_best_effort(pid_t pid, int signal) {
-    while (kill(pid, signal) != 0 && errno == EINTR) {
-    }
-}
-
-void reap_process_best_effort(pid_t pid) {
-    (void)waitpid_nointr(pid, nullptr, 0);
-}
-
-void exec_child_process(
-    const std::vector<std::string>& args,
-    const std::optional<fs::path>& cwd,
-    const std::vector<std::pair<std::string, std::string>>& env) {
-    std::vector<char*> argv = process_argv(args);
-    if (cwd.has_value() && chdir(cwd->c_str()) != 0) {
-        std::_Exit(126);
-    }
-    for (const auto& item : env) {
-        if (setenv(item.first.c_str(), item.second.c_str(), 1) != 0) {
-            std::_Exit(126);
-        }
-    }
-    execvp(argv[0], argv.data());
-    std::_Exit(127);
-}
-
-pid_t start_captured_process(
-    const std::vector<std::string>& args,
-    const std::optional<fs::path>& cwd,
-    const std::vector<std::pair<std::string, std::string>>& env,
-    int pipefd[2]) {
-    // Capture stdout and stderr through one pipe so probe/download callers can
-    // return useful diagnostics without leaking subprocess chatter onto yai's
-    // parseable stdout. The child gets its own process group so timeout cleanup
-    // can reach descendants as well as the direct process.
-    if (pipe(pipefd) != 0) {
-        throw std::runtime_error(std::string(tr("pipe failed: ")) + std::strerror(errno));
-    }
-
-    const pid_t pid = fork();
-    if (pid < 0) {
-        const int fork_errno = errno;
-        close_fd_best_effort(pipefd[0]);
-        close_fd_best_effort(pipefd[1]);
-        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(fork_errno));
-    }
-
-    if (pid == 0) {
-        if (setpgid(0, 0) != 0) {
-            std::_Exit(126);
-        }
-        if (close(pipefd[0]) != 0) {
-            std::_Exit(126);
-        }
-        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
-            dup2(pipefd[1], STDERR_FILENO) < 0 ||
-            close(pipefd[1]) != 0) {
-            std::_Exit(126);
-        }
-        exec_child_process(args, cwd, env);
-    }
-
-    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
-        const int setpgid_errno = errno;
-        close_fd_best_effort(pipefd[0]);
-        close_fd_best_effort(pipefd[1]);
-        signal_process_best_effort(pid, SIGKILL);
-        reap_process_best_effort(pid);
-        throw std::runtime_error(std::string(tr("setpgid failed: ")) + std::strerror(setpgid_errno));
-    }
-    if (close(pipefd[1]) != 0) {
-        const int close_errno = errno;
-        close_fd_best_effort(pipefd[0]);
-        signal_process_best_effort(-pid, SIGKILL);
-        signal_process_best_effort(pid, SIGKILL);
-        reap_process_best_effort(pid);
-        throw std::runtime_error(
-            std::string(tr("captured process setup: close failed: ")) + std::strerror(close_errno));
-    }
-    return pid;
-}
-
-void set_nonblocking(int fd) {
-    const int flags = fcntl(fd, F_GETFL, 0);
-    if (flags < 0) {
-        throw std::runtime_error(std::string(tr("fcntl F_GETFL failed: ")) + std::strerror(errno));
-    }
-    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
-        throw std::runtime_error(std::string(tr("fcntl F_SETFL failed: ")) + std::strerror(errno));
-    }
-}
-
-void kill_and_reap(pid_t pid) {
-    signal_process_best_effort(-pid, SIGKILL);
-    signal_process_best_effort(pid, SIGKILL);
-    reap_process_best_effort(pid);
-}
-
-enum class ReadOutputResult {
-    Data,
-    End,
-    RetryLater,
-};
-
-ReadOutputResult read_output_chunk(
-    int fd,
-    std::string& output,
-    bool nonblocking,
-    const char* buffer,
-    ssize_t count) {
-    if (count > 0) {
-        output.append(buffer, static_cast<std::size_t>(count));
-        return ReadOutputResult::Data;
-    }
-    if (count == 0) {
-        return ReadOutputResult::End;
-    }
-    if (errno == EINTR || (nonblocking && (errno == EAGAIN || errno == EWOULDBLOCK))) {
-        return ReadOutputResult::RetryLater;
-    }
-    close_fd_best_effort(fd);
-    throw std::runtime_error(std::string(tr("read failed: ")) + std::strerror(errno));
-}
-
-void append_output_until_stop(int fd, std::string& output, bool nonblocking) {
-    char buffer[4096];
-    while (true) {
-        const ssize_t count = read(fd, buffer, sizeof(buffer));
-        const ReadOutputResult result = read_output_chunk(fd, output, nonblocking, buffer, count);
-        if (result == ReadOutputResult::RetryLater && !nonblocking) {
-            continue;
-        }
-        if (result != ReadOutputResult::Data) {
-            break;
-        }
-    }
-}
-
-void append_available_output(int fd, std::string& output, pid_t pid) {
-    try {
-        append_output_until_stop(fd, output, true);
-    } catch (const std::exception&) {
-        kill_and_reap(pid);
-        throw;
-    }
-}
-
-void append_blocking_output(int fd, std::string& output) {
-    append_output_until_stop(fd, output, false);
-}
-
-void terminate_for_timeout(pid_t pid, int& status) {
-    signal_process_best_effort(-pid, SIGTERM);
-    signal_process_best_effort(pid, SIGTERM);
-    usleep(100 * 1000);
-    const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
-    if (wait_result == 0) {
-        signal_process_best_effort(-pid, SIGKILL);
-        signal_process_best_effort(pid, SIGKILL);
-        waitpid_required(pid, &status, 0, tr("timed-out process cleanup"));
-    } else if (wait_result == pid) {
-        signal_process_best_effort(-pid, SIGKILL);
-    } else {
-        signal_process_best_effort(-pid, SIGKILL);
-        signal_process_best_effort(pid, SIGKILL);
-    }
-}
-
-int run_process(const std::vector<std::string>& args) {
-    if (args.empty()) {
-        return 1;
-    }
-
-    const pid_t pid = fork();
-    if (pid < 0) {
-        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(errno));
-    }
-    if (pid == 0) {
-        exec_child_process(args, std::nullopt, {});
-    }
-
-    int status = 0;
-    waitpid_required(pid, &status, 0, tr("process"));
-    if (WIFEXITED(status)) {
-        return WEXITSTATUS(status);
-    }
-    return 128;
-}
-
-ProcessResult run_process_capture(
-    const std::vector<std::string>& args,
-    const std::optional<fs::path>& cwd,
-    const std::vector<std::pair<std::string, std::string>>& env) {
-    if (args.empty()) {
-        return ProcessResult{1, ""};
-    }
-
-    int pipefd[2];
-    const pid_t pid = start_captured_process(args, cwd, env, pipefd);
-    std::string output;
-    append_blocking_output(pipefd[0], output);
-    close_fd_required(pipefd[0], tr("captured process output"));
-
-    int status = 0;
-    waitpid_required(pid, &status, 0, tr("captured process"));
-    if (WIFEXITED(status)) {
-        return ProcessResult{WEXITSTATUS(status), output};
-    }
-    return ProcessResult{128, output};
-}
-
-ProcessOutput run_process_capture_separate(
-    const std::vector<std::string>& args,
-    const std::optional<fs::path>& cwd,
-    const std::vector<std::pair<std::string, std::string>>& env) {
-    if (args.empty()) {
-        return ProcessOutput{1, "", ""};
-    }
-
-    static std::atomic<unsigned long> capture_sequence{0};
-    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
-    const unsigned long sequence = capture_sequence.fetch_add(1);
-    const fs::path capture_dir =
-        fs::temp_directory_path() /
-        ("yai-capture-" + std::to_string(getpid()) + "-" +
-         std::to_string(now) + "-" + std::to_string(sequence));
-    const fs::path stdout_path = capture_dir / "stdout";
-    const fs::path stderr_path = capture_dir / "stderr";
-    ensure_directory(capture_dir);
-
-    const pid_t pid = fork();
-    if (pid < 0) {
-        const int fork_errno = errno;
-        remove_all_best_effort(capture_dir);
-        throw std::runtime_error(std::string(tr("fork failed: ")) + std::strerror(fork_errno));
-    }
-
-    if (pid == 0) {
-        if (setpgid(0, 0) != 0) {
-            std::_Exit(126);
-        }
-        const int stdout_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
-        const int stderr_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
-        if (stdout_fd < 0 || stderr_fd < 0 ||
-            dup2(stdout_fd, STDOUT_FILENO) < 0 ||
-            dup2(stderr_fd, STDERR_FILENO) < 0) {
-            std::_Exit(126);
-        }
-        close_fd_best_effort(stdout_fd);
-        close_fd_best_effort(stderr_fd);
-        exec_child_process(args, cwd, env);
-    }
-
-    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
-        const int setpgid_errno = errno;
-        signal_process_best_effort(pid, SIGKILL);
-        reap_process_best_effort(pid);
-        remove_all_best_effort(capture_dir);
-        throw std::runtime_error(std::string(tr("setpgid failed: ")) + std::strerror(setpgid_errno));
-    }
-
-    int status = 0;
-    try {
-        waitpid_required(pid, &status, 0, tr("captured process"));
-    } catch (const std::exception&) {
-        signal_process_best_effort(-pid, SIGKILL);
-        signal_process_best_effort(pid, SIGKILL);
-        remove_all_best_effort(capture_dir);
-        throw;
-    }
-
-    const std::string stdout_text = fs::exists(stdout_path) ? read_text_file(stdout_path) : "";
-    const std::string stderr_text = fs::exists(stderr_path) ? read_text_file(stderr_path) : "";
-    remove_all_best_effort(capture_dir);
-
-    if (WIFEXITED(status)) {
-        return ProcessOutput{WEXITSTATUS(status), stdout_text, stderr_text};
-    }
-    return ProcessOutput{128, stdout_text, stderr_text};
-}
-
-ProcessResult run_process_capture_timeout(
-    const std::vector<std::string>& args,
-    int timeout_ms,
-    const std::optional<fs::path>& cwd,
-    const std::vector<std::pair<std::string, std::string>>& env) {
-    if (args.empty()) {
-        return ProcessResult{1, ""};
-    }
-
-    int pipefd[2];
-    const pid_t pid = start_captured_process(args, cwd, env, pipefd);
-    try {
-        set_nonblocking(pipefd[0]);
-    } catch (const std::exception&) {
-        close_fd_best_effort(pipefd[0]);
-        kill_and_reap(pid);
-        throw;
-    }
-
-    std::string output;
-    bool timed_out = false;
-    int status = 0;
-    bool exited = false;
-    int elapsed_ms = 0;
-    constexpr int step_ms = 50;
-
-    // Nonblocking reads keep a GUI AppImage probe from hanging forever: collect
-    // whatever output exists, poll waitpid, and only then enforce the timeout.
-    while (true) {
-        append_available_output(pipefd[0], output, pid);
-
-        const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
-        if (wait_result == pid) {
-            exited = true;
-            break;
-        }
-        if (wait_result < 0) {
-            close_fd_best_effort(pipefd[0]);
-            throw std::runtime_error(std::string(tr("captured process poll: waitpid failed: ")) + std::strerror(errno));
-        }
-        if (elapsed_ms >= timeout_ms) {
-            timed_out = true;
-            terminate_for_timeout(pid, status);
-            break;
-        }
-        usleep(step_ms * 1000);
-        elapsed_ms += step_ms;
-    }
-
-    append_available_output(pipefd[0], output, pid);
-    close_fd_required(pipefd[0], tr("captured process output"));
-
-    if (timed_out) {
-        return ProcessResult{0, output, true};
-    }
-    if (exited && WIFEXITED(status)) {
-        return ProcessResult{WEXITSTATUS(status), output, false};
-    }
-    return ProcessResult{128, output, false};
-}
-
 std::string format_byte_count(std::uintmax_t bytes) {
     const char* units[] = {"B", "KiB", "MiB", "GiB"};
     double value = static_cast<double>(bytes);
@@ -1150,67 +769,6 @@
     last_width = 0;
 }
 
-ProcessResult run_process_capture_download_progress(
-    const std::vector<std::string>& args,
-    const fs::path& part,
-    const fs::path& headers) {
-    if (args.empty()) {
-        return ProcessResult{1, ""};
-    }
-
-    int pipefd[2];
-    const pid_t pid = start_captured_process(args, std::nullopt, {}, pipefd);
-    try {
-        set_nonblocking(pipefd[0]);
-    } catch (const std::exception&) {
-        close_fd_best_effort(pipefd[0]);
-        kill_and_reap(pid);
-        throw;
-    }
-
-    std::string output;
-    std::size_t last_width = 0;
-    DownloadProgressState progress_state;
-    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
-    int tick = 0;
-    int status = 0;
-    bool exited = false;
-    while (!exited) {
-        // curl output remains captured for errors. yai renders its own progress
-        // from the growing .part file and dumped headers, so stdout stays owned
-        // by the command result.
-        try {
-            append_available_output(pipefd[0], output, pid);
-        } catch (const std::exception&) {
-            clear_download_progress(last_width);
-            throw;
-        }
-
-        const pid_t wait_result = waitpid_nointr(pid, &status, WNOHANG);
-        if (wait_result == pid) {
-            exited = true;
-            break;
-        }
-        if (wait_result < 0) {
-            close_fd_best_effort(pipefd[0]);
-            clear_download_progress(last_width);
-            throw std::runtime_error(std::string(tr("download process poll: waitpid failed: ")) + std::strerror(errno));
-        }
-        render_download_progress(part, headers, start, tick, last_width, progress_state);
-        ++tick;
-        usleep(200 * 1000);
-    }
-
-    append_available_output(pipefd[0], output, pid);
-    close_fd_required(pipefd[0], tr("download process output"));
-    clear_download_progress(last_width);
-
-    if (WIFEXITED(status)) {
-        return ProcessResult{WEXITSTATUS(status), output};
-    }
-    return ProcessResult{128, output};
-}
-
 void ensure_directory(const fs::path& path) {
     std::error_code ec;
     fs::create_directories(path, ec);
## NEW src/process.cpp
#include "yai.hpp"

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
};

ReadOutputResult read_output_chunk(
    int fd,
    std::string& output,
    bool nonblocking,
    const char* buffer,
    ssize_t count) {
    if (count > 0) {
        output.append(buffer, static_cast<std::size_t>(count));
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

void append_output_until_stop(int fd, std::string& output, bool nonblocking) {
    char buffer[4096];
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        const ReadOutputResult result = read_output_chunk(fd, output, nonblocking, buffer, count);
        if (result == ReadOutputResult::RetryLater && !nonblocking) {
            continue;
        }
        if (result != ReadOutputResult::Data) {
            break;
        }
    }
}

void append_available_output(int fd, std::string& output, pid_t pid) {
    try {
        append_output_until_stop(fd, output, true);
    } catch (const std::exception&) {
        kill_and_reap(pid);
        throw;
    }
}

void append_blocking_output(int fd, std::string& output) {
    append_output_until_stop(fd, output, false);
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
    const std::vector<std::pair<std::string, std::string>>& env) {
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
        append_available_output(pipefd[0], output, pid);

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

    append_available_output(pipefd[0], output, pid);
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
    const fs::path& headers) {
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
        // from the growing .part file and dumped headers, so stdout stays owned
        // by the command result.
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
        render_download_progress(part, headers, start, tick, last_width, progress_state);
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
## Symbol placement
src/core.cpp:270:std::string format_byte_count(std::uintmax_t bytes) {
src/core.cpp:764:void clear_download_progress(std::size_t& last_width) {
src/core.cpp:772:void ensure_directory(const fs::path& path) {
src/process.cpp:5:namespace {
src/process.cpp:6:std::vector<char*> process_argv(const std::vector<std::string>& args) {
src/process.cpp:212:} // namespace
src/process.cpp:214:int run_process(const std::vector<std::string>& args) {
src/process.cpp:235:ProcessResult run_process_capture(
src/process.cpp:257:ProcessOutput run_process_capture_separate(
src/process.cpp:327:ProcessResult run_process_capture_timeout(
src/process.cpp:388:ProcessResult run_process_capture_download_progress(
## Public APIs in process.cpp
214:int run_process(const std::vector<std::string>& args) {
235:ProcessResult run_process_capture(
257:ProcessOutput run_process_capture_separate(
327:ProcessResult run_process_capture_timeout(
388:ProcessResult run_process_capture_download_progress(
## Must NOT be in core anymore
(none — good)
## Progress must still be in core
270:std::string format_byte_count(std::uintmax_t bytes) {
741:void render_download_progress(
764:void clear_download_progress(std::size_t& last_width) {
