#include "document_extractor.h"

#include "knowledge_types.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <sys/wait.h>

namespace hydrastore {
namespace {

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
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        std::size_t width = 1;
        bool valid = false;
        if (c <= 0x7F) {
            valid = true;
        } else if (c >= 0xC2 && c <= 0xDF && i + 1 < input.size() &&
                   (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80) {
            width = 2; valid = true;
        } else if (c >= 0xE0 && c <= 0xEF && i + 2 < input.size() &&
                   (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i + 2]) & 0xC0) == 0x80) {
            width = 3; valid = true;
        } else if (c >= 0xF0 && c <= 0xF4 && i + 3 < input.size() &&
                   (static_cast<unsigned char>(input[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i + 2]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(input[i + 3]) & 0xC0) == 0x80) {
            width = 4; valid = true;
        }
        if (valid) output.append(input, i, width), i += width;
        else output.append("\xEF\xBF\xBD"), ++i;
    }
    return output;
}

bool WriteBounded(const char *data, std::size_t size, std::string *buffer,
                  bool *truncated) {
    const std::size_t room = buffer->size() < AI_MAX_EXTRACTED_BYTES
                                 ? AI_MAX_EXTRACTED_BYTES - buffer->size() : 0;
    const std::size_t take = std::min(room, size);
    if (take != 0) buffer->append(data, take);
    if (take != size) *truncated = true;
    return true;
}

bool RunFilter(const char *program, const std::vector<std::string> &args,
               std::string *output, bool *truncated) {
    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]); return false;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(program));
        for (const std::string &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(program, argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    char buffer[8192];
    for (;;) {
        const ssize_t count = read(pipefd[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            close(pipefd[0]); waitpid(pid, nullptr, 0); return false;
        }
        WriteBounded(buffer, static_cast<std::size_t>(count), output, truncated);
    }
    close(pipefd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;
    return true;
}

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
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd); unlink(name); return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    close(fd);
    document->text_path = name;
    document->extracted_bytes = static_cast<std::int64_t>(text.size());
    return true;
}

}  // namespace

bool ExtractDocument(const SourceObject &source, ExtractedDocument *document,
                     std::string *error) {
    if (error) error->clear();
    if (!document || source.local_path.empty()) {
        if (error) *error = "source path is empty";
        return false;
    }
    *document = ExtractedDocument();
    document->type = Lower(source.type);
    const std::string type = document->type;
    std::string content;
    bool truncated = false;

    if (type == "pdf") {
        if (!RunFilter("pdftotext", {"-enc", "UTF-8", source.local_path, "-"}, &content, &truncated)) {
            if (error) *error = "pdftotext failed";
            return false;
        }
    } else if (type == "docx") {
        std::string xml;
        if (!RunFilter("unzip", {"-p", source.local_path, "word/document.xml"}, &xml, &truncated)) {
            if (error) *error = "docx extraction failed";
            return false;
        }
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
        while (input && content.size() < AI_MAX_EXTRACTED_BYTES) {
            input.read(buffer, sizeof(buffer));
            const std::streamsize count = input.gcount();
            if (count > 0) content.append(buffer, static_cast<std::size_t>(count));
        }
        if (input && input.peek() != EOF) truncated = true;
    }
    content = SanitizeUtf8(content);
    if (content.size() > AI_MAX_EXTRACTED_BYTES) {
        content.resize(AI_MAX_EXTRACTED_BYTES);
        truncated = true;
    }
    document->truncated = truncated;
    if (!WriteTextFile(content, document)) {
        if (error) *error = "cannot write bounded extraction";
        return false;
    }
    return true;
}

}  // namespace hydrastore
