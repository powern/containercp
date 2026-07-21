#include "database/DatabaseDumpService.h"

#include "api/JsonFormatter.h"
#include "database/DatabaseIdentifierValidator.h"
#include "database/DatabaseLifecycleAudit.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <openssl/evp.h>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::database {
namespace {

namespace fs = std::filesystem;

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string epoch_iso(int64_t epoch) {
    std::time_t tt = static_cast<std::time_t>(epoch);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

std::vector<jobs::JobStep> dump_steps(const std::string& operation) {
    std::vector<std::string> names;
    if (operation == "export") {
        names = {"Validating database ownership", "Checking MariaDB runtime", "Preparing secure credentials",
                 "Preparing export staging", "Creating logical SQL dump", "Validating export artifact",
                 "Calculating artifact metadata", "Finalizing artifact", "Completed", "Cleaning partial artifact on failure"};
    } else {
        names = {"Validating database ownership", "Validating import artifact", "Checking MariaDB runtime",
                 "Preparing secure credentials", "Preparing recovery export if required", "Importing SQL",
                 "Verifying database access", "Cleaning staging", "Completed", "Manual recovery required"};
    }
    std::vector<jobs::JobStep> steps;
    for (const auto& name : names) {
        jobs::JobStep step;
        step.id = name;
        step.name = name;
        steps.push_back(std::move(step));
    }
    return steps;
}

void mark_step(std::vector<jobs::JobStep>& steps, const std::string& name, bool success, const std::string& code = {}) {
    for (auto& step : steps) {
        if (step.name == name) {
            step.started = true;
            step.skipped = false;
            step.completed = success;
            step.failed = !success;
            step.result = success ? "success" : "failure";
            step.error_code = code;
            return;
        }
    }
}

DatabaseDumpResult dump_failure(std::vector<jobs::JobStep> steps,
                                std::string step_name,
                                std::string code,
                                std::string message,
                                bool manual_recovery = false,
                                std::string recovery_artifact_id = {}) {
    mark_step(steps, step_name, false, code);
    DatabaseDumpResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    result.steps = std::move(steps);
    result.failure.step = step_name;
    result.failure.step_name = step_name;
    result.failure.reason = result.message;
    result.failure.error_code = result.code;
    result.failure.manual_recovery_required = manual_recovery;
    result.manual_recovery_required = manual_recovery;
    result.recovery_artifact_id = std::move(recovery_artifact_id);
    return result;
}

DatabaseDumpResult dump_success(std::vector<jobs::JobStep> steps, std::string code, std::string message, std::string artifact_id = {}) {
    mark_step(steps, "Completed", true);
    DatabaseDumpResult result;
    result.success = true;
    result.code = std::move(code);
    result.message = std::move(message);
    result.steps = std::move(steps);
    result.artifact_id = std::move(artifact_id);
    return result;
}

std::string env_value(const fs::path& path, const std::string& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::string prefix = key + "=";
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return {};
}

bool generic_confirmation(const std::string& value) {
    return value == "true" || value == "false" || value == "yes" || value == "no" || value == "import" || value == "restore";
}

bool confirmation_valid(const std::string& confirmation, const std::string& database_name, const std::string& domain) {
    if (confirmation.empty() || confirmation.size() > 253 || generic_confirmation(confirmation)) return false;
    for (unsigned char c : confirmation) if (std::iscntrl(c) != 0) return false;
    return confirmation == database_name || confirmation == domain;
}

bool ensure_private_dir(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) return false;
    (void)::chmod(path.c_str(), S_IRWXU);
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == ::geteuid() && (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

bool safe_file_stat(const fs::path& path, uint64_t& size) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_nlink != 1) return false;
    if (st.st_uid != ::geteuid()) return false;
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) return false;
    if (st.st_size <= 0) return false;
    size = static_cast<uint64_t>(st.st_size);
    return true;
}

std::string sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    char buffer[16384];
    while (in.good()) {
        in.read(buffer, sizeof(buffer));
        const auto got = in.gcount();
        if (got > 0 && EVP_DigestUpdate(ctx, buffer, static_cast<std::size_t>(got)) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    EVP_MD_CTX_free(ctx);
    std::ostringstream hex;
    for (unsigned int i = 0; i < digest_len; ++i) hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    return hex.str();
}

bool write_owner_file(const fs::path& path, const std::string& content) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            (void)::close(fd);
            return false;
        }
        if (written == 0) {
            (void)::close(fd);
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return ::fsync(fd) == 0 && ::close(fd) == 0;
}

std::string field(const std::string& key, const std::string& value) {
    std::string safe = value;
    safe.erase(std::remove(safe.begin(), safe.end(), '\n'), safe.end());
    return key + "=" + safe + "\n";
}

std::string lower_copy(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool contains_word_like(const std::string& lower, const std::string& needle) {
    return lower.find(needle) != std::string::npos;
}

void audit_dump(const std::string& operation,
                const std::string& stage,
                const std::string& result,
                const std::string& code,
                uint64_t job_id,
                uint64_t site_id,
                uint64_t database_id,
                const std::string& domain,
                const std::string& artifact_id,
                DatabaseLifecycleAuditEvent::Level level = DatabaseLifecycleAuditEvent::Level::Info) {
    (void)artifact_id;
    DatabaseLifecycleAuditLogger::log({operation, stage, result, code, job_id, site_id, database_id, domain, false, level});
}

} // namespace

bool database_artifact_id_valid(const std::string& artifact_id) {
    if (artifact_id.size() != 32) return false;
    for (unsigned char c : artifact_id) if (std::isxdigit(c) == 0) return false;
    return true;
}

std::string database_generate_artifact_id() {
    static constexpr char hex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 32; ++i) id.push_back(hex[dist(gen)]);
    return id;
}

std::string database_sanitize_dump_filename(const std::string& filename, const std::string& fallback_prefix) {
    std::string base;
    for (unsigned char c : filename) {
        const bool ok = std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
        base.push_back(ok ? static_cast<char>(c) : '_');
    }
    while (base.find("..") != std::string::npos) base.replace(base.find(".."), 2, "_");
    while (!base.empty() && (base.front() == '.' || base.front() == '_' || base.front() == '-')) base.erase(base.begin());
    if (base.empty()) base = fallback_prefix + ".sql";
    if (base.size() > 96) base = base.substr(0, 96);
    if (base.size() < 4 || base.substr(base.size() - 4) != ".sql") base += ".sql";
    return base;
}

bool database_import_content_policy_allows(const fs::path& path, std::string& code, std::string& message) {
    uint64_t size = 0;
    if (!safe_file_stat(path, size)) {
        code = "artifact_file_invalid";
        message = "Import artifact is not a safe regular file";
        return false;
    }
    if (size > DatabaseDumpService::kMaxImportSizeBytes) {
        code = "upload_too_large";
        message = "Import artifact exceeds the configured size limit";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("-- ContainerCP DB-4 logical export") == std::string::npos) {
        code = "unsupported_import_format";
        message = "Only ContainerCP-generated .sql exports are supported for DB-4 import";
        return false;
    }
    const std::string lower = lower_copy(content);
    const std::vector<std::string> rejected = {
        "create database", "drop database", "use `", "use ", "grant ", "create user", "alter user",
        "load data local infile", "\nsource ", "\n\\.", "definer=", " into outfile", " from infile"
    };
    for (const auto& needle : rejected) {
        if (contains_word_like(lower, needle)) {
            code = "unsupported_sql_construct";
            message = "Import artifact contains SQL outside the DB-4 safety policy";
            return false;
        }
    }
    return true;
}

std::string database_artifact_metadata_json(const DatabaseArtifactMetadata& m) {
    std::ostringstream json;
    json << "{\"artifact_id\":\"" << api::JsonFormatter::escape(m.artifact_id)
         << "\",\"database_id\":" << m.database_id
         << ",\"site_id\":" << m.site_id
         << ",\"job_id\":" << m.job_id
         << ",\"kind\":\"" << api::JsonFormatter::escape(m.kind)
         << "\",\"filename\":\"" << api::JsonFormatter::escape(m.sanitized_filename)
         << "\",\"size\":" << m.size
         << ",\"checksum\":\"" << api::JsonFormatter::escape(m.checksum_sha256)
         << "\",\"created_at\":\"" << api::JsonFormatter::escape(m.created_at)
         << "\",\"expires_at\":\"" << api::JsonFormatter::escape(m.expires_at)
         << "\",\"status\":\"" << api::JsonFormatter::escape(m.status)
         << "\",\"download_count\":" << m.download_count
         << ",\"cleanup_state\":" << (m.cleanup_state ? "true" : "false")
         << "}";
    return json.str();
}

DatabaseDumpService::DatabaseDumpService(site::SiteManager& sites,
                                         DatabaseManager& databases,
                                         runtime::SiteRuntimeManager& site_runtime,
                                         const DatabaseProvider& provider,
                                         fs::path sites_root,
                                         fs::path artifacts_root)
    : DatabaseDumpService(sites,
                          databases,
                          [&site_runtime](const site::Site& site_record) { return site_runtime.get_status(site_record.id, site_record.domain).db.status; },
                          provider,
                          std::move(sites_root),
                          std::move(artifacts_root)) {
    site_runtime_ = &site_runtime;
}

DatabaseDumpService::DatabaseDumpService(site::SiteManager& sites,
                                         DatabaseManager& databases,
                                         DatabaseDumpService::RuntimeDbStatusLookup runtime_lookup,
                                         const DatabaseProvider& provider,
                                         fs::path sites_root,
                                         fs::path artifacts_root)
    : sites_(sites)
    , databases_(databases)
    , runtime_lookup_(std::move(runtime_lookup))
    , provider_(provider)
    , sites_root_(std::move(sites_root))
    , artifacts_root_(std::move(artifacts_root)) {
    (void)ensure_private_dir(artifacts_root_);
}

bool DatabaseDumpService::site_has_exactly_one_database(uint64_t site_id) const {
    int count = 0;
    for (const auto& database : databases_.list()) if (database.site_id == site_id && database.enabled) ++count;
    return count == 1;
}

MariaDBConnectionTarget DatabaseDumpService::target_for_site(const site::Site& site_record) const {
    return {(sites_root_ / site_record.domain / "docker-compose.yml").string(), "mariadb"};
}

DatabaseProviderCredential DatabaseDumpService::load_service_account(const site::Site& site_record) const {
    const auto env_path = sites_root_ / site_record.domain / ".env";
    return {env_value(env_path, "CONTAINERCP_DB_SERVICE_USER"), env_value(env_path, "CONTAINERCP_DB_SERVICE_PASSWORD"), "localhost"};
}

DatabaseDumpResult DatabaseDumpService::resolve_target(uint64_t database_id, ResolvedTarget& resolved, const std::string& operation, uint64_t job_id) const {
    auto steps = dump_steps(operation);
    auto* database = databases_.find(database_id);
    if (database == nullptr) return dump_failure(std::move(steps), "Validating database ownership", "database_not_found", "Database was not found");
    auto* site_record = sites_.find_by_id(database->site_id);
    if (site_record == nullptr) return dump_failure(std::move(steps), "Validating database ownership", "site_not_found", "Site was not found");
    if (!can_transfer(*database)) return dump_failure(std::move(steps), "Validating database ownership", transfer_block_reason(*database), "Database is not eligible for DB-4 transfer");
    if (!DatabaseIdentifierValidator::validate_database_name(database->db_name).valid || !DatabaseIdentifierValidator::validate_user_name(database->db_user).valid) {
        return dump_failure(std::move(steps), "Validating database ownership", "identifier_invalid", "Database metadata contains unsupported identifiers");
    }
    mark_step(steps, "Validating database ownership", true);
    resolved.site_record = site_record;
    resolved.database = database;
    resolved.target = target_for_site(*site_record);
    resolved.service_account = load_service_account(*site_record);
    if (resolved.service_account.user.empty() || resolved.service_account.password.empty()) {
        return dump_failure(std::move(steps), "Preparing secure credentials", "service_account_unavailable", "ContainerCP MariaDB service account is unavailable for this site");
    }
    DatabaseDumpResult ok;
    ok.success = true;
    ok.steps = std::move(steps);
    audit_dump(operation, "validate", "success", {}, job_id, database->site_id, database_id, site_record->domain, {});
    return ok;
}

fs::path DatabaseDumpService::artifact_dir(const std::string& artifact_id) const { return artifacts_root_ / artifact_id; }
fs::path DatabaseDumpService::sql_path(const std::string& artifact_id) const { return artifact_dir(artifact_id) / "artifact.sql"; }
fs::path DatabaseDumpService::metadata_path(const std::string& artifact_id) const { return artifact_dir(artifact_id) / "metadata.txt"; }

bool DatabaseDumpService::write_metadata(const DatabaseArtifactMetadata& metadata) const {
    if (!database_artifact_id_valid(metadata.artifact_id) || !ensure_private_dir(artifact_dir(metadata.artifact_id))) return false;
    const auto path = metadata_path(metadata.artifact_id);
    const auto tmp = path.parent_path() / "metadata.tmp";
    std::ostringstream out;
    out << field("artifact_id", metadata.artifact_id)
        << field("database_id", std::to_string(metadata.database_id))
        << field("site_id", std::to_string(metadata.site_id))
        << field("job_id", std::to_string(metadata.job_id))
        << field("kind", metadata.kind)
        << field("sanitized_filename", metadata.sanitized_filename)
        << field("size", std::to_string(metadata.size))
        << field("checksum_sha256", metadata.checksum_sha256)
        << field("created_at", metadata.created_at)
        << field("expires_at", metadata.expires_at)
        << field("expires_at_epoch", std::to_string(metadata.expires_at_epoch))
        << field("status", metadata.status)
        << field("download_count", std::to_string(metadata.download_count))
        << field("cleanup_state", metadata.cleanup_state ? "1" : "0");
    if (!write_owner_file(tmp, out.str())) return false;
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) return false;
    return true;
}

std::optional<DatabaseArtifactMetadata> DatabaseDumpService::read_metadata(const std::string& artifact_id) const {
    if (!database_artifact_id_valid(artifact_id)) return std::nullopt;
    uint64_t ignored = 0;
    if (!safe_file_stat(metadata_path(artifact_id), ignored)) return std::nullopt;
    std::ifstream in(metadata_path(artifact_id));
    DatabaseArtifactMetadata m;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (key == "artifact_id") m.artifact_id = value;
        else if (key == "database_id") m.database_id = std::stoull(value);
        else if (key == "site_id") m.site_id = std::stoull(value);
        else if (key == "job_id") m.job_id = std::stoull(value);
        else if (key == "kind") m.kind = value;
        else if (key == "sanitized_filename") m.sanitized_filename = value;
        else if (key == "size") m.size = std::stoull(value);
        else if (key == "checksum_sha256") m.checksum_sha256 = value;
        else if (key == "created_at") m.created_at = value;
        else if (key == "expires_at") m.expires_at = value;
        else if (key == "expires_at_epoch") m.expires_at_epoch = std::stoll(value);
        else if (key == "status") m.status = value;
        else if (key == "download_count") m.download_count = std::stoull(value);
        else if (key == "cleanup_state") m.cleanup_state = value == "1";
    }
    if (m.artifact_id != artifact_id) return std::nullopt;
    return m;
}

DatabaseDumpResult DatabaseDumpService::finalize_export_artifact(const ResolvedTarget& target,
                                                                 uint64_t job_id,
                                                                 const std::string& artifact_id,
                                                                 const fs::path& tmp_path,
                                                                 std::vector<jobs::JobStep> steps,
                                                                 const std::string& kind) {
    uint64_t size = 0;
    if (!safe_file_stat(tmp_path, size)) return dump_failure(std::move(steps), "Validating export artifact", "artifact_file_invalid", "Export artifact is not a safe regular file");
    mark_step(steps, "Validating export artifact", true);
    const auto checksum = sha256_file(tmp_path);
    if (checksum.empty()) return dump_failure(std::move(steps), "Calculating artifact metadata", "checksum_failed", "Export checksum could not be calculated");
    mark_step(steps, "Calculating artifact metadata", true);
    const int64_t expires = now_epoch() + kDefaultExpirySeconds;
    DatabaseArtifactMetadata m;
    m.artifact_id = artifact_id;
    m.database_id = target.database->id;
    m.site_id = target.site_record->id;
    m.job_id = job_id;
    m.kind = kind;
    m.sanitized_filename = database_sanitize_dump_filename(target.database->db_name + "-" + kind + ".sql", "database");
    m.size = size;
    m.checksum_sha256 = checksum;
    m.created_at = now_iso();
    m.expires_at_epoch = expires;
    m.expires_at = epoch_iso(expires);
    m.status = "available";
    const auto final_path = sql_path(artifact_id);
    if (!ensure_private_dir(artifact_dir(artifact_id))) return dump_failure(std::move(steps), "Finalizing artifact", "artifact_storage_unavailable", "Artifact storage is unavailable");
    std::error_code ec;
    fs::rename(tmp_path, final_path, ec);
    if (ec || !write_metadata(m)) {
        (void)fs::remove(final_path);
        return dump_failure(std::move(steps), "Finalizing artifact", "artifact_finalize_failed", "Export artifact could not be finalized");
    }
    mark_step(steps, "Finalizing artifact", true);
    audit_dump(kind == "recovery" ? "import" : "export", "completed", "success", {}, job_id, target.site_record->id, target.database->id, target.site_record->domain, artifact_id);
    return dump_success(std::move(steps), "export_completed", kind == "recovery" ? "Recovery export created" : "Database export completed", artifact_id);
}

DatabaseDumpResult DatabaseDumpService::exportManagedDatabase(uint64_t database_id, uint64_t job_id, const std::string& artifact_id) {
    if (!database_artifact_id_valid(artifact_id)) return dump_failure(dump_steps("export"), "Preparing export staging", "artifact_id_invalid", "Artifact identifier is invalid");
    ResolvedTarget resolved;
    auto resolved_result = resolve_target(database_id, resolved, "export", job_id);
    auto steps = std::move(resolved_result.steps);
    if (!resolved_result.success) return resolved_result;
    if (runtime_lookup_(*resolved.site_record) != "Running") return dump_failure(std::move(steps), "Checking MariaDB runtime", "runtime_unavailable", "MariaDB runtime is not running");
    mark_step(steps, "Checking MariaDB runtime", true);
    if (!provider_.verify_service_account(resolved.target, resolved.service_account).success) return dump_failure(std::move(steps), "Preparing secure credentials", "service_account_unavailable", "MariaDB service account verification failed");
    mark_step(steps, "Preparing secure credentials", true);
    if (!ensure_private_dir(artifact_dir(artifact_id))) return dump_failure(std::move(steps), "Preparing export staging", "artifact_storage_unavailable", "Artifact storage is unavailable");
    const auto tmp_path = artifact_dir(artifact_id) / "artifact.tmp";
    mark_step(steps, "Preparing export staging", true);
    auto exported = provider_.export_database(resolved.target, resolved.database->db_name, resolved.database->db_user, resolved.database->db_password, tmp_path.string());
    if (!exported.success) {
        (void)fs::remove(tmp_path);
        mark_step(steps, "Cleaning partial artifact on failure", true);
        return dump_failure(std::move(steps), "Creating logical SQL dump", exported.code, "Database export failed: " + exported.message);
    }
    mark_step(steps, "Creating logical SQL dump", true);
    return finalize_export_artifact(resolved, job_id, artifact_id, tmp_path, std::move(steps), "export");
}

DatabaseDumpResult DatabaseDumpService::create_recovery_export(const ResolvedTarget& target, uint64_t job_id) {
    const std::string id = database_generate_artifact_id();
    auto steps = dump_steps("export");
    mark_step(steps, "Validating database ownership", true);
    mark_step(steps, "Checking MariaDB runtime", true);
    mark_step(steps, "Preparing secure credentials", true);
    if (!ensure_private_dir(artifact_dir(id))) return dump_failure(std::move(steps), "Preparing export staging", "artifact_storage_unavailable", "Artifact storage is unavailable");
    const auto tmp_path = artifact_dir(id) / "artifact.tmp";
    mark_step(steps, "Preparing export staging", true);
    auto exported = provider_.export_database(target.target, target.database->db_name, target.database->db_user, target.database->db_password, tmp_path.string());
    if (!exported.success) {
        (void)fs::remove(tmp_path);
        return dump_failure(std::move(steps), "Creating logical SQL dump", exported.code, "Recovery export failed: " + exported.message);
    }
    mark_step(steps, "Creating logical SQL dump", true);
    return finalize_export_artifact(target, job_id, id, tmp_path, std::move(steps), "recovery");
}

DatabaseDumpResult DatabaseDumpService::importManagedDatabase(uint64_t database_id, uint64_t job_id, const std::string& artifact_id, const std::string& confirmation) {
    ResolvedTarget resolved;
    auto resolved_result = resolve_target(database_id, resolved, "import", job_id);
    auto steps = std::move(resolved_result.steps);
    if (!resolved_result.success) return resolved_result;
    if (!confirmation_valid(confirmation, resolved.database->db_name, resolved.site_record->domain)) {
        audit_dump("import", "confirmation", "rejected", "confirmation_mismatch", job_id, resolved.site_record->id, database_id, resolved.site_record->domain, artifact_id, DatabaseLifecycleAuditEvent::Level::Warning);
        return dump_failure(std::move(steps), "Validating database ownership", "confirmation_mismatch", "Confirmation must match the database name or site domain");
    }
    auto meta = artifact(database_id, artifact_id);
    if (!meta) return dump_failure(std::move(steps), "Validating import artifact", "artifact_not_found", "Import artifact was not found or is unavailable");
    std::string code, message;
    const auto import_path = sql_path(artifact_id);
    if (!database_import_content_policy_allows(import_path, code, message)) return dump_failure(std::move(steps), "Validating import artifact", code, message);
    mark_step(steps, "Validating import artifact", true);
    if (runtime_lookup_(*resolved.site_record) != "Running") return dump_failure(std::move(steps), "Checking MariaDB runtime", "runtime_unavailable", "MariaDB runtime is not running");
    mark_step(steps, "Checking MariaDB runtime", true);
    if (!provider_.verify_service_account(resolved.target, resolved.service_account).success) return dump_failure(std::move(steps), "Preparing secure credentials", "service_account_unavailable", "MariaDB service account verification failed");
    mark_step(steps, "Preparing secure credentials", true);
    auto recovery = create_recovery_export(resolved, job_id);
    if (!recovery.success) return dump_failure(std::move(steps), "Preparing recovery export if required", recovery.code, "Pre-import recovery export failed; import was not started");
    mark_step(steps, "Preparing recovery export if required", true);
    audit_dump("import", "started", "accepted", {}, job_id, resolved.site_record->id, database_id, resolved.site_record->domain, artifact_id);
    auto imported = provider_.import_sql_file(resolved.target, resolved.database->db_name, resolved.database->db_user, resolved.database->db_password, import_path.string());
    if (!imported.success) {
        mark_step(steps, "Manual recovery required", true, "failed_target_may_be_partial");
        return dump_failure(std::move(steps), "Importing SQL", imported.code, "Database import failed; target may be partially modified; recovery export is available", true, recovery.artifact_id);
    }
    mark_step(steps, "Importing SQL", true);
    if (!provider_.verify_login(resolved.target, resolved.database->db_name, resolved.database->db_user, resolved.database->db_password).success ||
        !provider_.database_exists(resolved.target, resolved.service_account, resolved.database->db_name).success) {
        mark_step(steps, "Manual recovery required", true, "failed_target_may_be_partial");
        return dump_failure(std::move(steps), "Verifying database access", "post_import_verification_failed", "Import ran but target verification failed; recovery export is available", true, recovery.artifact_id);
    }
    mark_step(steps, "Verifying database access", true);
    mark_step(steps, "Cleaning staging", true);
    audit_dump("import", "completed", "success", {}, job_id, resolved.site_record->id, database_id, resolved.site_record->domain, artifact_id);
    auto ok = dump_success(std::move(steps), "import_completed", "Database import completed", artifact_id);
    ok.recovery_artifact_id = recovery.artifact_id;
    return ok;
}

DatabaseUploadResult DatabaseDumpService::stageImportUpload(uint64_t database_id, const std::string& original_filename, const std::string& content) {
    DatabaseUploadResult result;
    result.database_id = database_id;
    auto* database = databases_.find(database_id);
    if (database == nullptr) return {false, "database_not_found", "Database was not found", {}, database_id, 0};
    auto* site_record = sites_.find_by_id(database->site_id);
    if (site_record == nullptr) return {false, "site_not_found", "Site was not found", {}, database_id, database->site_id};
    result.site_id = site_record->id;
    if (!can_transfer(*database)) return {false, transfer_block_reason(*database), "Database is not eligible for DB-4 transfer", {}, database_id, site_record->id};
    if (content.empty()) return {false, "upload_empty", "Import upload is empty", {}, database_id, site_record->id};
    if (content.size() > kMaxImportSizeBytes) return {false, "upload_too_large", "Import upload exceeds the configured size limit", {}, database_id, site_record->id};
    const auto filename = database_sanitize_dump_filename(original_filename, database->db_name + "-import");
    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".sql") return {false, "unsupported_import_format", "Only .sql imports are supported", {}, database_id, site_record->id};
    const std::string id = database_generate_artifact_id();
    if (!ensure_private_dir(artifact_dir(id))) return {false, "artifact_storage_unavailable", "Artifact storage is unavailable", {}, database_id, site_record->id};
    const auto path = sql_path(id);
    if (!write_owner_file(path, content)) return {false, "upload_write_failed", "Import upload could not be staged", {}, database_id, site_record->id};
    std::string code, message;
    if (!database_import_content_policy_allows(path, code, message)) {
        (void)fs::remove_all(artifact_dir(id));
        return {false, code, message, {}, database_id, site_record->id};
    }
    const int64_t expires = now_epoch() + kDefaultExpirySeconds;
    DatabaseArtifactMetadata m;
    m.artifact_id = id;
    m.database_id = database_id;
    m.site_id = site_record->id;
    m.kind = "import-upload";
    m.sanitized_filename = filename;
    m.size = static_cast<uint64_t>(content.size());
    m.checksum_sha256 = sha256_file(path);
    m.created_at = now_iso();
    m.expires_at_epoch = expires;
    m.expires_at = epoch_iso(expires);
    m.status = "available";
    if (!write_metadata(m)) {
        (void)fs::remove_all(artifact_dir(id));
        return {false, "metadata_persist_failed", "Import upload metadata could not be stored", {}, database_id, site_record->id};
    }
    audit_dump("import-upload", "accepted", "success", {}, 0, site_record->id, database_id, site_record->domain, id);
    return {true, "upload_staged", "Import upload staged", id, database_id, site_record->id};
}

std::optional<DatabaseArtifactMetadata> DatabaseDumpService::artifact(uint64_t database_id, const std::string& artifact_id) const {
    auto m = read_metadata(artifact_id);
    if (!m || m->database_id != database_id || m->status != "available" || m->cleanup_state || m->expires_at_epoch <= now_epoch()) return std::nullopt;
    uint64_t ignored = 0;
    if (!safe_file_stat(sql_path(artifact_id), ignored)) return std::nullopt;
    return m;
}

std::optional<fs::path> DatabaseDumpService::artifact_path(uint64_t database_id, const std::string& artifact_id) const {
    auto m = artifact(database_id, artifact_id);
    if (!m) return std::nullopt;
    return sql_path(artifact_id);
}

DatabaseUploadResult DatabaseDumpService::revokeArtifact(uint64_t database_id, const std::string& artifact_id) {
    auto m = read_metadata(artifact_id);
    if (!m || m->database_id != database_id) return {false, "artifact_not_found", "Artifact was not found", {}, database_id, 0};
    m->status = "revoked";
    m->cleanup_state = true;
    (void)write_metadata(*m);
    std::error_code ec;
    fs::remove(sql_path(artifact_id), ec);
    audit_dump("artifact", "revoked", "success", {}, 0, m->site_id, m->database_id, {}, artifact_id, DatabaseLifecycleAuditEvent::Level::Warning);
    return {true, "artifact_revoked", "Artifact revoked", artifact_id, m->database_id, m->site_id};
}

void DatabaseDumpService::record_download(uint64_t database_id, const std::string& artifact_id) {
    auto m = artifact(database_id, artifact_id);
    if (!m) return;
    ++m->download_count;
    (void)write_metadata(*m);
    audit_dump("artifact", "downloaded", "success", {}, m->job_id, m->site_id, m->database_id, {}, artifact_id);
}

void DatabaseDumpService::cleanup_expired() {
    std::error_code ec;
    if (!fs::exists(artifacts_root_, ec)) return;
    for (const auto& entry : fs::directory_iterator(artifacts_root_, ec)) {
        if (ec) break;
        const auto id = entry.path().filename().string();
        auto m = read_metadata(id);
        if (!m || m->cleanup_state || m->status != "available" || m->expires_at_epoch <= now_epoch()) {
            fs::remove_all(entry.path(), ec);
        }
    }
}

bool DatabaseDumpService::can_transfer(const Database& database) const {
    return database.enabled && !database.db_password.empty() && database.engine == "mariadb" && site_has_exactly_one_database(database.site_id);
}

std::string DatabaseDumpService::transfer_block_reason(const Database& database) const {
    if (!database.enabled) return "database_disabled";
    if (database.db_password.empty()) return "ownership_not_managed";
    if (database.engine != "mariadb") return "engine_not_supported";
    if (!site_has_exactly_one_database(database.site_id)) return "database_cardinality_invalid";
    return {};
}

} // namespace containercp::database
