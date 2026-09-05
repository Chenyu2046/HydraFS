#include "object_reader.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <openssl/md5.h>
#include <sstream>
#include <utility>
#include <vector>

namespace hydrastore {
namespace {

std::string Escape(MYSQL *connection, const std::string &value) {
    std::string escaped(value.size() * 2 + 1, '\0');
    const auto length = mysql_real_escape_string(
        connection, escaped.data(), value.data(), value.size());
    escaped.resize(length);
    return escaped;
}

struct FileSink {
    int fd = -1;
    std::int64_t remaining = 0;
    std::int64_t total = 0;
    MD5_CTX digest{};
};

bool WriteAll(void *context, const void *data, std::size_t size) {
    auto *sink = static_cast<FileSink *>(context);
    if (static_cast<std::int64_t>(size) > sink->remaining) return false;
    const auto *bytes = static_cast<const char *>(data);
    while (size > 0) {
        const ssize_t written = write(sink->fd, bytes, size);
        if (written > 0) {
            bytes += written;
            size -= static_cast<std::size_t>(written);
            sink->remaining -= written;
            sink->total += written;
            MD5_Update(&sink->digest, bytes - written, static_cast<std::size_t>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool QueryRows(MYSQL *connection, const std::string &sql,
               std::vector<std::vector<std::string>> *rows) {
    if (mysql_query(connection, sql.c_str()) != 0) return false;
    MYSQL_RES *result = mysql_store_result(connection);
    if (!result) return false;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        std::vector<std::string> values;
        const unsigned int fields = mysql_num_fields(result);
        values.reserve(fields);
        for (unsigned int i = 0; i < fields; ++i) {
            values.emplace_back(row[i] ? row[i] : "");
        }
        rows->push_back(std::move(values));
    }
    mysql_free_result(result);
    return true;
}

}  // namespace

ObjectReader::ObjectReader(MYSQL *connection, BlobStore *blobs)
    : connection_(connection), blobs_(blobs) {}

void ObjectReader::SetConnection(MYSQL *connection) {
    connection_ = connection;
}

bool ObjectReader::DownloadToFile(const std::string &content_digest,
                                  const std::string &user,
                                  const std::string &output_path) {
    if (!connection_ || !blobs_ || content_digest.empty() || user.empty() ||
        output_path.empty()) {
        return false;
    }

    const std::string escaped_digest = Escape(connection_, content_digest);
    const std::string escaped_user = Escape(connection_, user);
    std::vector<std::vector<std::string>> manifest_rows;
    const std::string manifest_sql =
        "SELECT COALESCE(f.manifest_id,0),m.total_size,m.content_digest,m.chunk_count "
        "FROM file_info f JOIN user_file_list u ON u.md5=f.md5 "
        "JOIN object_manifest m ON m.id=f.manifest_id "
        "WHERE f.md5='" + escaped_digest + "' AND u.user='" + escaped_user +
        "' AND f.storage_mode='manifest' AND m.state='COMMITTED' LIMIT 1";
    if (!QueryRows(connection_, manifest_sql, &manifest_rows) || manifest_rows.empty() ||
        manifest_rows[0].size() < 4 || manifest_rows[0][0].empty() ||
        manifest_rows[0][2] != content_digest) {
        return false;
    }

    std::vector<std::vector<std::string>> chunk_rows;
    const std::string chunk_sql =
        "SELECT mc.size,c.size,c.state,COALESCE(c.backend_file_id,'') FROM manifest_chunk mc "
        "JOIN chunk_blob c ON c.id=mc.chunk_id "
        "WHERE mc.manifest_id=" + manifest_rows[0][0] +
        " ORDER BY mc.part_index";
    if (!QueryRows(connection_, chunk_sql, &chunk_rows) || chunk_rows.empty() ||
        std::stoll(manifest_rows[0][3]) != static_cast<std::int64_t>(chunk_rows.size())) {
        return false;
    }

    const int fd = open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;

    FileSink file_sink{fd};
    MD5_Init(&file_sink.digest);
    BlobSink sink;
    sink.context = &file_sink;
    sink.write = WriteAll;

    bool ok = true;
    for (const auto &row : chunk_rows) {
        if (row.size() < 4 || row[0].empty() || row[1].empty() || row[2] != "READY" ||
            row[3].empty()) {
            ok = false;
            break;
        }
        file_sink.remaining = std::stoll(row[0]);
        std::int64_t actual_size = 0;
        if (!blobs_->Get(row[3], sink, 0, 0, &actual_size) ||
            file_sink.remaining != 0 || actual_size != std::stoll(row[1])) {
            ok = false;
            break;
        }
    }
    unsigned char digest_bytes[MD5_DIGEST_LENGTH] = {0};
    MD5_Final(digest_bytes, &file_sink.digest);
    std::ostringstream digest;
    digest << std::hex << std::setfill('0');
    for (unsigned char byte : digest_bytes) digest << std::setw(2) << static_cast<int>(byte);
    if (ok && file_sink.total != std::stoll(manifest_rows[0][1])) ok = false;
    if (ok && digest.str() != content_digest) ok = false;
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (!ok) unlink(output_path.c_str());
    return ok;
}

}  // namespace hydrastore
