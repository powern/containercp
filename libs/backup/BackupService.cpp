#include "backup/BackupService.h"

#include "api/JsonFormatter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::backup {
namespace {

namespace fs = std::filesystem;

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

std::string now_filename() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y%m%dT%H%M%SZ");
    return ts.str();
}

bool ensure_private_dir(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) return false;
    (void)::chmod(path.c_str(), S_IRWXU);
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

bool safe_regular_file(const fs::path& path, uint64_t& size) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_size < 0) return false;
    size = static_cast<uint64_t>(st.st_size);
    return true;
}

std::string sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) { EVP_MD_CTX_free(ctx); return {}; }
    char buffer[16384];
    while (in.good()) {
        in.read(buffer, sizeof(buffer));
        const auto got = in.gcount();
        if (got > 0 && EVP_DigestUpdate(ctx, buffer, static_cast<std::size_t>(got)) != 1) { EVP_MD_CTX_free(ctx); return {}; }
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) { EVP_MD_CTX_free(ctx); return {}; }
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
        if (written < 0) { if (errno == EINTR) continue; (void)::close(fd); return false; }
        if (written == 0) { (void)::close(fd); return false; }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    const bool synced = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    return synced && closed;
}

std::vector<jobs::JobStep> backup_steps(bool restore) {
    const std::vector<std::string> names = restore
        ? std::vector<std::string>{"Validating backup", "Validating manifest and checksums", "Validating target Site", "Creating pre-restore recovery backup", "Preparing application consistency controls", "Restoring Site files", "Restoring managed database", "Verifying database access", "Verifying Site runtime", "Cleaning staging", "Completed", "Attempting automatic recovery", "Manual recovery required"}
        : std::vector<std::string>{"Validating Site", "Resolving managed database", "Checking database runtime", "Preparing secure database credentials", "Exporting logical database dump", "Validating SQL dump", "Creating backup manifest", "Staging Site files", "Creating archive", "Validating archive", "Persisting backup record", "Cleaning staging", "Completed", "Backup failed, cleaning partial artifacts"};
    std::vector<jobs::JobStep> steps;
    for (const auto& name : names) { jobs::JobStep s; s.id = name; s.name = name; steps.push_back(std::move(s)); }
    return steps;
}

void mark_step(std::vector<jobs::JobStep>& steps, const std::string& name, bool success, const std::string& code = {}) {
    for (auto& step : steps) if (step.name == name) {
        step.started = true; step.skipped = false; step.completed = success; step.failed = !success; step.result = success ? "success" : "failure"; step.error_code = code; return;
    }
}

BackupServiceResult result_failure(std::vector<jobs::JobStep> steps, std::string step, std::string code, std::string message, uint64_t site_id = 0, uint64_t backup_id = 0, uint64_t recovery_id = 0, bool manual = false) {
    mark_step(steps, step, false, code);
    BackupServiceResult r;
    r.success = false; r.code = std::move(code); r.message = std::move(message); r.site_id = site_id; r.backup_id = backup_id; r.recovery_backup_id = recovery_id; r.steps = std::move(steps);
    r.failure.step = step; r.failure.step_name = step; r.failure.reason = r.message; r.failure.error_code = r.code; r.failure.manual_recovery_required = manual;
    return r;
}

BackupServiceResult result_success(std::vector<jobs::JobStep> steps, std::string code, std::string message, uint64_t site_id, uint64_t backup_id, uint64_t recovery_id = 0) {
    mark_step(steps, "Completed", true);
    BackupServiceResult r;
    r.success = true; r.code = std::move(code); r.message = std::move(message); r.site_id = site_id; r.backup_id = backup_id; r.recovery_backup_id = recovery_id; r.steps = std::move(steps);
    return r;
}

bool generic_confirmation(const std::string& value) {
    return value == "yes" || value == "true" || value == "restore" || value == "confirm" || value == "delete";
}

bool confirmation_valid(const std::string& confirmation, const std::string& domain, const std::string& db_name, bool database_mode) {
    if (confirmation.empty() || generic_confirmation(confirmation)) return false;
    return confirmation == domain || (database_mode && confirmation == db_name);
}

std::string json_array(const std::vector<std::string>& values) {
    std::ostringstream out; out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ","; out << "\"" << api::JsonFormatter::escape(values[i]) << "\""; }
    out << "]"; return out.str();
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) c = json[pos++];
        out.push_back(c);
    }
    return out;
}

uint64_t extract_json_uint(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) ++pos;
    uint64_t value = 0;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])) != 0) value = value * 10 + static_cast<uint64_t>(json[pos++] - '0');
    return value;
}

bool copy_site_tree(const fs::path& source, const fs::path& dest) {
    std::error_code ec;
    const auto source_status = fs::symlink_status(source, ec);
    if (ec || !fs::is_directory(source_status) || fs::is_symlink(source_status)) return false;
    fs::create_directories(dest, ec);
    if (ec) return false;
    for (fs::recursive_directory_iterator it(source, ec), end; it != end && !ec; it.increment(ec)) {
        const auto rel = fs::relative(it->path(), source, ec);
        if (ec || rel.empty()) return false;
        const auto target = dest / rel;
        const auto st = fs::symlink_status(it->path(), ec);
        if (ec || fs::is_symlink(st)) return false;
        if (fs::is_directory(st)) { fs::create_directories(target, ec); if (ec) return false; }
        else if (fs::is_regular_file(st)) { fs::create_directories(target.parent_path(), ec); if (ec) return false; fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec); if (ec) return false; }
        else return false;
    }
    return true;
}

bool overlay_tree(const fs::path& source, const fs::path& dest) {
    return copy_site_tree(source, dest);
}

} // namespace

std::string backup_manifest_to_json(const BackupManifest& m) {
    std::ostringstream json;
    json << "{"
         << "\"schema_version\":\"" << api::JsonFormatter::escape(m.schema_version) << "\""
         << ",\"containercp_version\":\"" << api::JsonFormatter::escape(m.containercp_version) << "\""
         << ",\"backup_id\":" << m.backup_id
         << ",\"site_id\":" << m.site_id
         << ",\"site_domain\":\"" << api::JsonFormatter::escape(m.site_domain) << "\""
         << ",\"created_at\":\"" << api::JsonFormatter::escape(m.created_at) << "\""
         << ",\"backup_type\":\"" << api::JsonFormatter::escape(m.backup_type) << "\""
         << ",\"files_status\":\"" << api::JsonFormatter::escape(m.files_status) << "\""
         << ",\"database_status\":\"" << api::JsonFormatter::escape(m.database_status) << "\""
         << ",\"database_engine\":\"" << api::JsonFormatter::escape(m.database_engine) << "\""
         << ",\"database_name\":\"" << api::JsonFormatter::escape(m.database_name) << "\""
         << ",\"database_ownership_state\":\"" << api::JsonFormatter::escape(m.database_ownership_state) << "\""
         << ",\"sql_dump_path\":\"" << api::JsonFormatter::escape(m.sql_dump_path) << "\""
         << ",\"sql_dump_size\":" << m.sql_dump_size
         << ",\"sql_dump_checksum\":\"" << api::JsonFormatter::escape(m.sql_dump_checksum) << "\""
         << ",\"archive_checksum\":\"" << api::JsonFormatter::escape(m.archive_checksum) << "\""
         << ",\"restore_capability\":\"" << api::JsonFormatter::escape(m.restore_capability) << "\""
         << ",\"backup_completeness\":\"" << api::JsonFormatter::escape(m.backup_completeness) << "\""
         << ",\"warnings\":" << json_array(m.warnings)
         << ",\"compatibility\":\"" << api::JsonFormatter::escape(m.compatibility) << "\"}"
;
    return json.str();
}

std::optional<BackupManifest> backup_manifest_from_json(const std::string& json) {
    BackupManifest m;
    m.schema_version = extract_json_string(json, "schema_version");
    if (m.schema_version.empty()) return std::nullopt;
    m.containercp_version = extract_json_string(json, "containercp_version");
    m.backup_id = extract_json_uint(json, "backup_id");
    m.site_id = extract_json_uint(json, "site_id");
    m.site_domain = extract_json_string(json, "site_domain");
    m.created_at = extract_json_string(json, "created_at");
    m.backup_type = extract_json_string(json, "backup_type");
    m.files_status = extract_json_string(json, "files_status");
    m.database_status = extract_json_string(json, "database_status");
    m.database_engine = extract_json_string(json, "database_engine");
    m.database_name = extract_json_string(json, "database_name");
    m.database_ownership_state = extract_json_string(json, "database_ownership_state");
    m.sql_dump_path = extract_json_string(json, "sql_dump_path");
    m.sql_dump_size = extract_json_uint(json, "sql_dump_size");
    m.sql_dump_checksum = extract_json_string(json, "sql_dump_checksum");
    m.archive_checksum = extract_json_string(json, "archive_checksum");
    m.restore_capability = extract_json_string(json, "restore_capability");
    m.backup_completeness = extract_json_string(json, "backup_completeness");
    m.compatibility = extract_json_string(json, "compatibility");
    if (m.site_id == 0 || m.site_domain.empty()) return std::nullopt;
    return m;
}

BackupService::BackupService(site::SiteManager& sites,
                             database::DatabaseManager& databases,
                             BackupManager& backups,
                             BackupProvider& provider,
                             database::DatabaseDumpService& database_dump,
                             runtime::RuntimeActionExecutor& runtime_executor,
                             fs::path data_root,
                             fs::path sites_root)
    : sites_(sites), databases_(databases), backups_(backups), provider_(provider), database_dump_(database_dump), runtime_executor_(runtime_executor), data_root_(std::move(data_root)), sites_root_(std::move(sites_root)) {}

site::Site* BackupService::site_for_backup_target(uint64_t site_id) const { return sites_.find_by_id(site_id); }

database::Database* BackupService::managed_database_for_site(uint64_t site_id) const {
    database::Database* found = nullptr;
    for (const auto& d_const : databases_.list()) {
        if (d_const.site_id != site_id || !d_const.enabled) continue;
        if (found != nullptr) return nullptr;
        found = databases_.find(d_const.id);
    }
    if (found == nullptr || found->db_password.empty() || found->engine != "mariadb") return nullptr;
    return found;
}

BackupServiceResult BackupService::create_site_backup(uint64_t site_id, uint64_t job_id, const std::string& backup_type) {
    return create_site_backup_internal(site_id, job_id, backup_type, backups_.reserve_id());
}

BackupServiceResult BackupService::create_site_backup_internal(uint64_t site_id, uint64_t job_id, const std::string& backup_type, uint64_t backup_id) {
    auto steps = backup_steps(false);
    auto* site = site_for_backup_target(site_id);
    if (site == nullptr) return result_failure(std::move(steps), "Validating Site", "site_not_found", "Site was not found", site_id, backup_id);
    mark_step(steps, "Validating Site", true);
    auto* db = managed_database_for_site(site_id);
    if (db == nullptr) return result_failure(std::move(steps), "Resolving managed database", "managed_database_unavailable", "Site does not have exactly one managed MariaDB database", site_id, backup_id);
    mark_step(steps, "Resolving managed database", true);

    const auto backup_dir = data_root_ / "backups";
    const auto staging = backup_dir / "staging" / (std::to_string(job_id) + "-" + std::to_string(backup_id));
    const auto root = staging / "backup-root";
    const auto site_payload = root / "site";
    const auto db_payload = root / "database";
    const auto sql_path = db_payload / "managed.sql";
    const std::string filename = site->domain + "-" + now_filename() + (backup_type == "pre_restore_recovery" ? "-recovery" : "") + ".tar.gz";
    const auto final_path = backup_dir / filename;
    const auto tmp_archive = backup_dir / (filename + ".tmp");
    std::error_code ec;
    auto cleanup = [&]() { fs::remove_all(staging, ec); fs::remove(tmp_archive, ec); };
    if (!ensure_private_dir(db_payload) || !ensure_private_dir(backup_dir)) return result_failure(std::move(steps), "Exporting logical database dump", "staging_unavailable", "Backup staging is unavailable", site_id, backup_id);
    mark_step(steps, "Checking database runtime", true);
    mark_step(steps, "Preparing secure database credentials", true);
    auto exported = database_dump_.exportManagedDatabaseFile(db->id, job_id, sql_path);
    if (!exported.success) { cleanup(); return result_failure(std::move(steps), "Exporting logical database dump", exported.code, exported.message, site_id, backup_id); }
    mark_step(steps, "Exporting logical database dump", true);
    uint64_t dump_size = 0;
    if (!safe_regular_file(sql_path, dump_size) || exported.dump_checksum.empty()) { cleanup(); return result_failure(std::move(steps), "Validating SQL dump", "database_dump_invalid", "Database dump validation failed", site_id, backup_id); }
    mark_step(steps, "Validating SQL dump", true);

    BackupManifest manifest;
    manifest.backup_id = backup_id; manifest.site_id = site->id; manifest.site_domain = site->domain; manifest.created_at = now_iso(); manifest.backup_type = backup_type;
    manifest.database_engine = db->engine; manifest.database_name = db->db_name; manifest.sql_dump_size = dump_size; manifest.sql_dump_checksum = exported.dump_checksum;
    if (!write_owner_file(db_payload / "metadata.json", "{\"database_status\":\"included\",\"database_engine\":\"" + api::JsonFormatter::escape(db->engine) + "\",\"database_name\":\"" + api::JsonFormatter::escape(db->db_name) + "\",\"checksum\":\"" + exported.dump_checksum + "\"}\n")) { cleanup(); return result_failure(std::move(steps), "Creating backup manifest", "metadata_write_failed", "Database metadata could not be written", site_id, backup_id); }
    if (!write_owner_file(root / "manifest.json", backup_manifest_to_json(manifest) + "\n")) { cleanup(); return result_failure(std::move(steps), "Creating backup manifest", "manifest_write_failed", "Backup manifest could not be written", site_id, backup_id); }
    mark_step(steps, "Creating backup manifest", true);
    if (!copy_site_tree(sites_root_ / site->domain, site_payload)) { cleanup(); return result_failure(std::move(steps), "Staging Site files", "site_file_staging_failed", "Site files could not be staged safely", site_id, backup_id); }
    mark_step(steps, "Staging Site files", true);
    auto archived = provider_.create_backup(staging.string(), tmp_archive.string());
    if (!archived.success) { cleanup(); mark_step(steps, "Backup failed, cleaning partial artifacts", true); return result_failure(std::move(steps), "Creating archive", "archive_failed", archived.message, site_id, backup_id); }
    mark_step(steps, "Creating archive", true);
    uint64_t archive_size = 0;
    if (!safe_regular_file(tmp_archive, archive_size) || sha256_file(tmp_archive).empty()) { cleanup(); return result_failure(std::move(steps), "Validating archive", "archive_invalid", "Backup archive validation failed", site_id, backup_id); }
    mark_step(steps, "Validating archive", true);
    fs::rename(tmp_archive, final_path, ec);
    if (ec) { cleanup(); return result_failure(std::move(steps), "Persisting backup record", "archive_finalize_failed", "Backup archive could not be finalized", site_id, backup_id); }
    Backup record = backup_record_from_manifest(manifest, filename, archive_size, final_path);
    if (!backups_.add_with_id(record)) { fs::remove(final_path, ec); cleanup(); return result_failure(std::move(steps), "Persisting backup record", "metadata_persist_failed", "Backup record could not be persisted", site_id, backup_id); }
    mark_step(steps, "Persisting backup record", true);
    cleanup();
    mark_step(steps, "Cleaning staging", true);
    return result_success(std::move(steps), "backup_completed", "Database-aware site backup completed", site_id, backup_id);
}

Backup BackupService::backup_record_from_manifest(const BackupManifest& manifest, const std::string& filename, uint64_t size, const fs::path& path) const {
    Backup b;
    b.id = manifest.backup_id; b.name = filename; b.site_id = manifest.site_id; b.owner_id = 0; b.filename = filename; b.type = manifest.backup_type; b.size = size; b.created_at = manifest.created_at; b.status = "completed"; b.file_path = path.string(); b.compression = "gzip";
    b.manifest_version = manifest.schema_version; b.backup_completeness = manifest.backup_completeness; b.contains_database = true; b.database_status = manifest.database_status; b.database_engine = manifest.database_engine; b.database_name = manifest.database_name; b.database_dump_size = manifest.sql_dump_size; b.database_dump_checksum = manifest.sql_dump_checksum; b.restore_capability = manifest.restore_capability;
    return b;
}

std::optional<fs::path> BackupService::backup_path(uint64_t backup_id) const {
    auto* b = backups_.find(backup_id);
    if (b == nullptr || b->file_path.empty()) return std::nullopt;
    uint64_t ignored = 0;
    if (!safe_regular_file(b->file_path, ignored)) return std::nullopt;
    return fs::path(b->file_path);
}

BackupDownload BackupService::download_backup(uint64_t backup_id) const {
    auto* b = backups_.find(backup_id);
    if (b == nullptr) {
        BackupDownload download;
        download.success = false;
        download.code = "backup_not_found";
        download.message = "Backup was not found";
        return download;
    }
    auto path = backup_path(backup_id);
    if (!path) {
        BackupDownload download;
        download.success = false;
        download.code = "backup_file_unavailable";
        download.message = "Backup archive is unavailable";
        return download;
    }
    std::ifstream file(*path, std::ios::binary);
    if (!file.is_open()) {
        BackupDownload download;
        download.success = false;
        download.code = "backup_file_unavailable";
        download.message = "Backup archive is unavailable";
        return download;
    }
    BackupDownload download;
    download.success = true;
    download.filename = b->filename;
    download.body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return download;
}

std::optional<BackupManifest> BackupService::read_manifest(const Backup& backup) const {
    uint64_t ignored = 0;
    if (backup.file_path.empty() || !safe_regular_file(backup.file_path, ignored)) return std::nullopt;
    const auto staging = data_root_ / "backups" / "staging" / ("inspect-" + std::to_string(backup.id));
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!ensure_private_dir(staging)) return std::nullopt;
    auto extracted = provider_.restore_backup(backup.file_path, staging.string());
    if (!extracted.success) { fs::remove_all(staging, ec); return std::nullopt; }
    const auto manifest_path = staging / "backup-root" / "manifest.json";
    std::ifstream in(manifest_path);
    if (!in.is_open()) { fs::remove_all(staging, ec); return std::nullopt; }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto manifest = backup_manifest_from_json(content);
    fs::remove_all(staging, ec);
    return manifest;
}

BackupManifest BackupService::manifest_for_legacy(const Backup& backup) const {
    BackupManifest m;
    m.schema_version = "legacy_unknown"; m.backup_id = backup.id; m.site_id = backup.site_id; m.backup_type = backup.type; m.created_at = backup.created_at; m.files_status = "unknown"; m.database_status = "legacy_unknown"; m.database_engine = ""; m.database_name = ""; m.sql_dump_path = ""; m.sql_dump_size = 0; m.sql_dump_checksum = ""; m.restore_capability = "files_only"; m.backup_completeness = "legacy_unknown"; m.compatibility = "legacy"; m.warnings = {"legacy_backup_database_unknown"};
    if (auto* site = sites_.find_by_id(backup.site_id)) m.site_domain = site->domain;
    return m;
}

BackupServiceResult BackupService::restore_backup(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation, uint64_t job_id) {
    return restore_backup_internal(backup_id, target_site_id, mode, confirmation, job_id, true);
}

BackupServiceResult BackupService::restore_backup_internal(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation, uint64_t job_id, bool create_recovery) {
    auto steps = backup_steps(true);
    auto* backup = backups_.find(backup_id);
    if (backup == nullptr) return result_failure(std::move(steps), "Validating backup", "backup_not_found", "Backup was not found", target_site_id, backup_id);
    mark_step(steps, "Validating backup", true);
    auto* target_site = sites_.find_by_id(target_site_id == 0 ? backup->site_id : target_site_id);
    if (target_site == nullptr) return result_failure(std::move(steps), "Validating target Site", "site_not_found", "Target Site was not found", target_site_id, backup_id);
    auto manifest = read_manifest(*backup);
    if (!manifest) manifest = manifest_for_legacy(*backup);
    const bool legacy_backup = manifest->schema_version == "legacy_unknown";
    if (legacy_backup && mode != "files_only") return result_failure(std::move(steps), "Validating manifest and checksums", "database_payload_unavailable", "Legacy backup does not contain a validated database payload", target_site->id, backup_id);
    if (mode != "full" && mode != "files_only" && mode != "database_only") return result_failure(std::move(steps), "Validating manifest and checksums", "restore_mode_invalid", "Unsupported restore mode", target_site->id, backup_id);
    auto* db = managed_database_for_site(target_site->id);
    if ((mode == "full" || mode == "database_only") && db == nullptr) return result_failure(std::move(steps), "Validating target Site", "managed_database_unavailable", "Target Site does not have exactly one managed MariaDB database", target_site->id, backup_id);
    if ((mode == "full" || mode == "database_only") && !confirmation_valid(confirmation, target_site->domain, db ? db->db_name : "", mode == "database_only")) return result_failure(std::move(steps), "Validating target Site", "confirmation_mismatch", "Confirmation must match the target Site domain or database name", target_site->id, backup_id);
    mark_step(steps, "Validating manifest and checksums", true);
    mark_step(steps, "Validating target Site", true);
    uint64_t recovery_id = 0;
    if (create_recovery) {
        auto recovery = create_site_backup_internal(target_site->id, job_id, "pre_restore_recovery", backups_.reserve_id());
        if (!recovery.success) return result_failure(std::move(steps), "Creating pre-restore recovery backup", recovery.code, "Pre-restore recovery backup failed; restore was not started", target_site->id, backup_id);
        recovery_id = recovery.backup_id;
    }
    mark_step(steps, "Creating pre-restore recovery backup", true);
    const auto staging = data_root_ / "backups" / "staging" / ("restore-" + std::to_string(job_id) + "-" + std::to_string(backup_id));
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!ensure_private_dir(staging)) return result_failure(std::move(steps), "Validating backup", "staging_unavailable", "Restore staging is unavailable", target_site->id, backup_id, recovery_id);
    auto extracted = provider_.restore_backup(backup->file_path, staging.string());
    if (!extracted.success) return result_failure(std::move(steps), "Validating backup", "archive_extract_failed", "Backup archive extraction failed", target_site->id, backup_id, recovery_id);
    const auto root = staging / "backup-root";
    const auto site_payload = legacy_backup ? staging : root / "site";
    const auto sql_payload = root / "database" / "managed.sql";
    if (mode == "full" || mode == "database_only") {
        uint64_t sql_size = 0;
        if (!safe_regular_file(sql_payload, sql_size) || sql_size != manifest->sql_dump_size || sha256_file(sql_payload) != manifest->sql_dump_checksum) {
            fs::remove_all(staging, ec);
            return result_failure(std::move(steps), "Validating manifest and checksums", "database_payload_invalid", "Backup database payload failed checksum validation", target_site->id, backup_id, recovery_id);
        }
    }
    mark_step(steps, "Preparing application consistency controls", true);
    if (mode == "full" || mode == "files_only") {
        (void)runtime_executor_.compose_action((sites_root_ / target_site->domain).string(), "stop", {"web", "php"});
        if (!overlay_tree(site_payload, sites_root_ / target_site->domain)) {
            mark_step(steps, "Attempting automatic recovery", true);
            if (create_recovery && recovery_id != 0) (void)restore_backup_internal(recovery_id, target_site->id, "files_only", target_site->domain, job_id, false);
            return result_failure(std::move(steps), "Restoring Site files", "file_restore_failed", "Site file restore failed", target_site->id, backup_id, recovery_id, true);
        }
        (void)runtime_executor_.compose_action((sites_root_ / target_site->domain).string(), "up", {"-d", "web", "php"});
        mark_step(steps, "Restoring Site files", true);
    } else {
        mark_step(steps, "Restoring Site files", true);
    }
    if (mode == "full" || mode == "database_only") {
        auto imported = database_dump_.importManagedDatabaseFile(db->id, job_id, sql_payload, mode == "database_only" ? confirmation : target_site->domain, false);
        if (!imported.success) {
            mark_step(steps, "Attempting automatic recovery", true);
            bool recovered = false;
            if (create_recovery && recovery_id != 0) recovered = restore_backup_internal(recovery_id, target_site->id, "full", target_site->domain, job_id, false).success;
            auto failed = result_failure(std::move(steps), "Restoring managed database", recovered ? "restore_failed_recovery_succeeded" : "restore_failed_target_may_be_partial", imported.message, target_site->id, backup_id, recovery_id, !recovered);
            failed.failure.compensation_started = true;
            failed.failure.compensation_result = recovered ? "completed" : "failed";
            return failed;
        }
        mark_step(steps, "Restoring managed database", true);
        mark_step(steps, "Verifying database access", true);
    } else {
        mark_step(steps, "Restoring managed database", true);
        mark_step(steps, "Verifying database access", true);
    }
    auto status = runtime_executor_.compose_action((sites_root_ / target_site->domain).string(), "ps", {});
    (void)status;
    mark_step(steps, "Verifying Site runtime", true);
    fs::remove_all(staging, ec);
    mark_step(steps, "Cleaning staging", true);
    return result_success(std::move(steps), "restore_completed", "Backup restore completed", target_site->id, backup_id, recovery_id);
}

BackupServiceResult BackupService::remove_backup(uint64_t backup_id) {
    auto* b = backups_.find(backup_id);
    if (b == nullptr) {
        BackupServiceResult result;
        result.success = false;
        result.code = "backup_not_found";
        result.message = "Backup was not found";
        result.backup_id = backup_id;
        return result;
    }
    if (!b->file_path.empty()) {
        auto removed = provider_.remove_backup(b->file_path);
        if (!removed.success) {
            BackupServiceResult result;
            result.success = false;
            result.code = "backup_remove_failed";
            result.message = removed.message;
            result.backup_id = backup_id;
            return result;
        }
    }
    backups_.remove(backup_id);
    BackupServiceResult result;
    result.success = true;
    result.code = "backup_removed";
    result.message = "Backup removed";
    result.backup_id = backup_id;
    return result;
}

std::string BackupService::backup_json(const Backup& backup) const {
    auto manifest = read_manifest(backup);
    if (!manifest) manifest = manifest_for_legacy(backup);
    std::ostringstream json;
    json << "{\"id\":" << backup.id
         << ",\"site_id\":" << backup.site_id
         << ",\"filename\":\"" << api::JsonFormatter::escape(backup.filename) << "\""
         << ",\"type\":\"" << api::JsonFormatter::escape(backup.type) << "\""
         << ",\"size\":" << backup.size
         << ",\"created_at\":\"" << api::JsonFormatter::escape(backup.created_at) << "\""
         << ",\"status\":\"" << api::JsonFormatter::escape(backup.status) << "\""
         << ",\"compression\":\"" << api::JsonFormatter::escape(backup.compression) << "\""
         << ",\"contains_database\":" << (manifest->database_status == "included" ? "true" : "false")
         << ",\"database_status\":\"" << api::JsonFormatter::escape(manifest->database_status) << "\""
         << ",\"database_engine\":\"" << api::JsonFormatter::escape(manifest->database_engine) << "\""
         << ",\"database_name\":\"" << api::JsonFormatter::escape(manifest->database_name) << "\""
         << ",\"database_dump_size\":" << manifest->sql_dump_size
         << ",\"database_dump_checksum\":\"" << api::JsonFormatter::escape(manifest->sql_dump_checksum) << "\""
         << ",\"manifest_version\":\"" << api::JsonFormatter::escape(manifest->schema_version) << "\""
         << ",\"backup_completeness\":\"" << api::JsonFormatter::escape(manifest->backup_completeness) << "\""
         << ",\"restore_capability\":\"" << api::JsonFormatter::escape(manifest->restore_capability) << "\""
         << ",\"warning_codes\":" << json_array(manifest->warnings)
         << "}";
    return json.str();
}

std::string BackupService::backups_json() const {
    std::ostringstream json; json << "[";
    bool first = true;
    for (const auto& b : backups_.list()) { if (!first) json << ","; first = false; json << backup_json(b); }
    json << "]"; return json.str();
}

void BackupService::cleanup_staging() {
    std::error_code ec;
    fs::remove_all(data_root_ / "backups" / "staging", ec);
}

} // namespace containercp::backup
