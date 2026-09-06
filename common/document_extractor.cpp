#include "document_extractor.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace hydrastore {
namespace {

constexpr int kPollIntervalMs = 50;
constexpr int kKillGraceMs = 250;
constexpr rlim_t kChildCpuLimitSeconds = 30;
constexpr rlim_t kChildAddressSpaceBytes = 512ULL * 1024ULL * 1024ULL;
constexpr rlim_t kChildFileSizeBytes = 32ULL * 1024ULL * 1024ULL;

std::string Lower(std::string value) {
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool IsSupportedTextType(const std::string &type) {
    static const char *const types[] = {
        "txt", "md", "csv", "json", "xml", "html", "htm", "log",
        "c", "cpp", "h", "hpp", "py", "js", "ts", "jsx", "tsx",
        "css", "java", "go", "rs", "rust", "rb", "php", "sh", "bat",
        "yaml", "yml", nullptr
    };
    for (const char *candidate : types) if (candidate && type == candidate) return true;
    return false;
}

std::string SanitizeUtf8(const std::string &input) {
    std::string output;
    output.reserve(input.size());
    const auto continuation = [&input](std::size_t index) {
        return index < input.size() &&
               (static_cast<unsigned char>(input[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        std::size_t width = 1;
        bool valid = false;
        if (c <= 0x7F) {
            valid = true;
        } else if (c >= 0xC2 && c <= 0xDF && continuation(i + 1)) {
            width = 2; valid = true;
        } else if (c >= 0xE0 && c <= 0xEF && continuation(i + 1) && continuation(i + 2) &&
                   ((c != 0xE0 && c != 0xED) ||
                    (c == 0xE0 && static_cast<unsigned char>(input[i + 1]) >= 0xA0) ||
                    (c == 0xED && static_cast<unsigned char>(input[i + 1]) <= 0x9F))) {
            width = 3; valid = true;
        } else if (c >= 0xF0 && c <= 0xF4 && continuation(i + 1) && continuation(i + 2) &&
                   continuation(i + 3) &&
                   ((c != 0xF0 && c != 0xF4) ||
                    (c == 0xF0 && static_cast<unsigned char>(input[i + 1]) >= 0x90) ||
                    (c == 0xF4 && static_cast<unsigned char>(input[i + 1]) <= 0x8F))) {
            width = 4; valid = true;
        }
        if (valid) output.append(input, i, width), i += width;
        else output.append("\xEF\xBF\xBD"), ++i;
    }
    return output;
}

std::size_t Utf8Width(const std::string &text, std::size_t offset) {
    const unsigned char c = static_cast<unsigned char>(text[offset]);
    if (c <= 0x7F) return 1;
    if (c >= 0xC2 && c <= 0xDF) return 2;
    if (c >= 0xE0 && c <= 0xEF) return 3;
    return 4;
}

void TruncateUtf8(std::string *text, std::size_t max_bytes, bool *truncated) {
    if (!text || text->size() <= max_bytes) return;
    std::size_t boundary = 0;
    while (boundary < text->size()) {
        const std::size_t width = Utf8Width(*text, boundary);
        if (boundary + width > max_bytes) break;
        boundary += width;
    }
    text->resize(boundary);
    if (truncated) *truncated = true;
}

void SetChildLimit(int resource, rlim_t value) {
    struct rlimit limit;
    limit.rlim_cur = value;
    limit.rlim_max = value;
    (void)setrlimit(resource, &limit);
}

void ReapAfterSignal(pid_t pid, int *status) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kKillGraceMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(pid, status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) return;
        if (result < 0 && errno != EINTR) break;
        (void)poll(nullptr, 0, 10);
    }
    (void)kill(-pid, SIGKILL);
    (void)kill(pid, SIGKILL);
    while (waitpid(pid, status, 0) < 0 && errno == EINTR) {}
}

}  // namespace

FilterResult RunFilter(const std::string &program,
                       const std::vector<std::string> &args,
                       std::size_t max_output_bytes, int timeout_ms,
                       std::string *output) {
    FilterResult result;
    if (output) output->clear();
    if (program.empty() || !output || max_output_bytes == 0 || timeout_ms <= 0) return result;

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) return result;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (pid == 0) {
        (void)setpgid(0, 0);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(126);
        const int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        SetChildLimit(RLIMIT_CPU, kChildCpuLimitSeconds);
        SetChildLimit(RLIMIT_AS, kChildAddressSpaceBytes);
        SetChildLimit(RLIMIT_FSIZE, kChildFileSizeBytes);
        std::vector<char *> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char *>(program.c_str()));
        for (const std::string &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(program.c_str(), argv.data());
        _exit(127);
    }

    (void)setpgid(pid, pid);
    close(pipefd[1]);
    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)kill(-pid, SIGTERM);
        (void)kill(pid, SIGTERM);
        int status = 0;
        ReapAfterSignal(pid, &status);
        close(pipefd[0]);
        if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
        return result;
    }

    bool eof = false;
    bool reaped = false;
    bool failed_read = false;
    bool terminated = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    auto drain = [&]() {
        char buffer[8192];
        while (!eof) {
            const ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
            if (count > 0) {
                const std::size_t room = output->size() < max_output_bytes
                    ? max_output_bytes - output->size() : 0;
                const std::size_t take = std::min(room, static_cast<std::size_t>(count));
                if (take != 0) output->append(buffer, take);
                if (take != static_cast<std::size_t>(count)) {
                    result.truncated = true;
                    return;
                }
                continue;
            }
            if (count == 0) { eof = true; return; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            failed_read = true;
            return;
        }
    };

    while (!eof || !reaped) {
        drain();
        if (result.truncated || failed_read) {
            terminated = true;
            (void)kill(-pid, SIGTERM);
            (void)kill(pid, SIGTERM);
            ReapAfterSignal(pid, &status);
            reaped = true;
            break;
        }
        if (!reaped) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) reaped = true;
            else if (waited < 0 && errno != EINTR) {
                terminated = true;
                (void)kill(-pid, SIGTERM);
                (void)kill(pid, SIGTERM);
                ReapAfterSignal(pid, &status);
                reaped = true;
            }
        }
        if (eof && reaped) break;
        if (!reaped && std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            terminated = true;
            (void)kill(-pid, SIGTERM);
            (void)kill(pid, SIGTERM);
            ReapAfterSignal(pid, &status);
            reaped = true;
            break;
        }
        int wait_ms = kPollIntervalMs;
        if (!reaped) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            wait_ms = static_cast<int>(std::max<long long>(1, std::min<long long>(wait_ms, remaining)));
        }
        struct pollfd descriptor = {pipefd[0], POLLIN | POLLHUP, 0};
        (void)poll(&descriptor, 1, wait_ms);
    }

    if (!reaped) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!eof && !result.truncated) drain();
    close(pipefd[0]);
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    result.ok = !failed_read && !result.timed_out &&
                ((WIFEXITED(status) && result.exit_code == 0) ||
                 (result.truncated && terminated));
    return result;
}

namespace {

std::string DocxText(const std::string &xml) {
    std::string text;
    bool in_tag = false;
    for (char c : xml) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; text.push_back(' '); continue; }
        if (!in_tag) text.push_back(c);
    }
    const std::string replacements[][2] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}
    };
    for (const auto &replacement : replacements) {
        std::size_t position = 0;
        while ((position = text.find(replacement[0], position)) != std::string::npos) {
            text.replace(position, replacement[0].size(), replacement[1]);
            position += replacement[1].size();
        }
    }
    return text;
}

bool WriteTextFile(const std::string &text, ExtractedDocument *document) {
    char name[] = "/tmp/hydra_ai_extract_XXXXXX";
    const int fd = mkstemp(name);
    if (fd < 0) return false;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t written = write(fd, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { close(fd); unlink(name); return false; }
        offset += static_cast<std::size_t>(written);
    }
    const bool synced = fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    if (!synced || !closed) {
        unlink(name);
        return false;
    }
    document->text_path = name;
    document->extracted_bytes = static_cast<std::int64_t>(text.size());
    return true;
}

}  // namespace

bool ExtractDocument(const SourceObject &source, ExtractedDocument *document,
                     std::string *error, const ExtractorOptions &options) {
    if (error) error->clear();
    if (!document || source.local_path.empty()) {
        if (error) *error = "source path is empty";
        return false;
    }
    const std::size_t max_output = options.max_output_bytes == 0
        ? static_cast<std::size_t>(AI_MAX_EXTRACTED_BYTES) : options.max_output_bytes;
    const int timeout_ms = std::max(1, options.timeout_ms);
    *document = ExtractedDocument();
    document->type = Lower(source.type);
    const std::string type = document->type;
    std::string content;
    bool truncated = false;

    if (type == "pdf") {
        const FilterResult filtered = RunFilter("pdftotext", {"-enc", "UTF-8", source.local_path, "-"},
                                                max_output, timeout_ms, &content);
        if (!filtered.ok) {
            if (error) *error = filtered.timed_out ? "pdftotext timeout" : "pdftotext failed";
            return false;
        }
        truncated = filtered.truncated;
    } else if (type == "docx") {
        std::string xml;
        const FilterResult filtered = RunFilter("unzip", {"-p", source.local_path, "word/document.xml"},
                                                max_output, timeout_ms, &xml);
        if (!filtered.ok) {
            if (error) *error = filtered.timed_out ? "docx extraction timeout" : "docx extraction failed";
            return false;
        }
        truncated = filtered.truncated;
        content = DocxText(xml);
    } else if (type == "png" || type == "jpg" || type == "jpeg" || type == "gif" ||
               type == "bmp" || type == "webp") {
        if (error) *error = "image requires multimodal extraction";
        return false;
    } else if (!IsSupportedTextType(type)) {
        if (error) *error = "unsupported document type";
        return false;
    } else {
        std::ifstream input(source.local_path, std::ios::binary);
        if (!input) { if (error) *error = "cannot open source"; return false; }
        char buffer[8192];
        while (content.size() < max_output) {
            input.read(buffer, static_cast<std::streamsize>(
                std::min<std::size_t>(sizeof(buffer), max_output - content.size())));
            const std::streamsize count = input.gcount();
            if (count <= 0) break;
            content.append(buffer, static_cast<std::size_t>(count));
        }
        if (content.size() == max_output) {
            char extra = 0;
            input.read(&extra, 1);
            truncated = input.gcount() > 0;
        }
    }

    content = SanitizeUtf8(content);
    TruncateUtf8(&content, max_output, &truncated);
    document->truncated = truncated;
    if (!WriteTextFile(content, document)) {
        if (error) *error = "cannot write bounded extraction";
        return false;
    }
    return true;
}

}  // namespace hydrastore
