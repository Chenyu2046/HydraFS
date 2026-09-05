#include "metadata_store.h"

#include <mysql/mysql.h>

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *EnvOr(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool Raw(MYSQL *db, const std::string &sql) {
    return mysql_query(db, sql.c_str()) == 0;
}

std::string Scalar(MYSQL *db, const std::string &sql) {
    if (!Raw(db, sql)) return {};
    MYSQL_RES *result = mysql_store_result(db);
    if (!result) return {};
    MYSQL_ROW row = mysql_fetch_row(result);
    const std::string value = row && row[0] ? row[0] : "";
    mysql_free_result(result);
    return value;
}

}  // namespace

int main() {
    const std::string host = EnvOr("HYDRA_TEST_DB_HOST", "tc_mysql");
    const unsigned int port = static_cast<unsigned int>(std::strtoul(
        EnvOr("HYDRA_TEST_DB_PORT", "3306"), nullptr, 10));
    const std::string db_user = EnvOr("HYDRA_TEST_DB_USER", "root");
    const std::string db_password = EnvOr("HYDRA_TEST_DB_PASSWORD", "123456");
    const std::string database = EnvOr("HYDRA_TEST_DB_NAME", "yuncunchu");
    const std::string user = "lease_test_user";
    const std::string suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string upload_a = "lease-test-a-" + suffix;
    const std::string upload_b = "lease-test-b-" + suffix;
    std::string digest(32, 'a');
    std::string sha256(64, 'a');
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char hex[] = "0123456789abcdef";
        digest[i % digest.size()] = hex[static_cast<unsigned char>(suffix[i]) % 16];
        sha256[i % sha256.size()] = hex[static_cast<unsigned char>(suffix[i]) % 16];
    }

    MYSQL *db = mysql_init(nullptr);
    assert(db && mysql_real_connect(db, host.c_str(), db_user.c_str(), db_password.c_str(),
                                    database.c_str(), port, nullptr, 0));
    const std::string cleanup = "DELETE FROM upload_session WHERE id IN ('" + upload_a + "','" + upload_b + "')";
    Raw(db, cleanup);

    hydrastore::MetadataStore store(host, port, db_user, db_password, database);
    hydrastore::PartSpec part{0, 11, sha256};
    hydrastore::UploadSession session_a;
    std::vector<hydrastore::PartStatus> statuses;
    assert(store.InitOrResume(upload_a, user, "stale.bin", 11, digest, {part},
                              &session_a, &statuses));
    hydrastore::PartClaim claim_a;
    assert(store.ClaimPartUpload(upload_a, 0, &claim_a));

    hydrastore::UploadSession session_b;
    assert(store.InitOrResume(upload_b, user, "takeover.bin", 11, digest, {part},
                              &session_b, &statuses));
    assert(Raw(db, "UPDATE chunk_blob SET lease_until=DATE_SUB(NOW(),INTERVAL 1 SECOND) WHERE id=" +
                    std::to_string(claim_a.chunk_id)));

    hydrastore::PartClaim claim_b;
    assert(store.ClaimPartUpload(upload_b, 0, &claim_b));
    assert(claim_b.lease_epoch > claim_a.lease_epoch);
    assert(!store.MarkPartReady(upload_a, 0, upload_a, claim_a.lease_epoch, "stale-backend"));
    assert(!store.MarkPartFailed(upload_a, 0, upload_a, claim_a.lease_epoch));

    const std::string owner_epoch = Scalar(
        db, "SELECT CONCAT(owner_upload_id,':',lease_epoch,':',state) FROM chunk_blob WHERE id=" +
            std::to_string(claim_b.chunk_id));
    assert(owner_epoch == upload_b + ":" + std::to_string(claim_b.lease_epoch) + ":UPLOADING");
    assert(store.RecordPartBackend(upload_b, 0, upload_b, claim_b.lease_epoch, "takeover-backend"));
    assert(store.MarkPartReady(upload_b, 0, upload_b, claim_b.lease_epoch, "takeover-backend"));
    assert(!store.MarkPartFailed(upload_a, 0, upload_a, claim_a.lease_epoch));
    assert(store.Commit(upload_b, user, &session_b));
    assert(store.Abort(upload_a, user));
    assert(store.DeleteObjectForUser(session_b.object_id, user));

    // The object delete above intentionally leaves a grace-period GC row. For
    // a deterministic test cleanup, remove only this fixture's rows after
    // asserting the production fencing sequence; GC itself has separate tests.
    assert(Raw(db, "DELETE FROM upload_part WHERE upload_id IN ('" + upload_a + "','" + upload_b + "')"));
    assert(Raw(db, "DELETE FROM upload_session WHERE id IN ('" + upload_a + "','" + upload_b + "')"));
    assert(Raw(db, "DELETE FROM chunk_blob WHERE id=" + std::to_string(claim_b.chunk_id) +
                    " AND ref_count=0 AND NOT EXISTS (SELECT 1 FROM manifest_chunk WHERE chunk_id=" +
                    std::to_string(claim_b.chunk_id) + ")"));

    const std::string upload_c = "lease-test-concurrent-" + suffix;
    hydrastore::UploadSession session_c;
    assert(store.InitOrResume(upload_c, user, "concurrent.bin", 11, digest, {part},
                              &session_c, &statuses));
    hydrastore::MetadataStore store_c(host, port, db_user, db_password, database);
    hydrastore::MetadataStore store_d(host, port, db_user, db_password, database);
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    hydrastore::PartClaim claim_c;
    hydrastore::PartClaim claim_d;
    std::thread first([&] {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();
        store_c.ClaimPartUpload(upload_c, 0, &claim_c);
    });
    std::thread second([&] {
        ready.fetch_add(1);
        while (!go.load()) std::this_thread::yield();
        store_d.ClaimPartUpload(upload_c, 0, &claim_d);
    });
    while (ready.load() != 2) std::this_thread::yield();
    go.store(true);
    first.join();
    second.join();
    assert(claim_c.granted != claim_d.granted);
    const auto concurrent_chunk_id = claim_c.granted ? claim_c.chunk_id : claim_d.chunk_id;
    assert(Raw(db, "DELETE FROM upload_part WHERE upload_id='" + upload_c + "'"));
    assert(Raw(db, "DELETE FROM upload_session WHERE id='" + upload_c + "'"));
    assert(Raw(db, "DELETE FROM chunk_blob WHERE id=" + std::to_string(concurrent_chunk_id) +
                    " AND ref_count=0 AND NOT EXISTS (SELECT 1 FROM manifest_chunk WHERE chunk_id=" +
                    std::to_string(concurrent_chunk_id) + ")"));
    mysql_close(db);
    std::cout << "PASS: stale lease takeover fences old ready/failed writers\n";
    return 0;
}
