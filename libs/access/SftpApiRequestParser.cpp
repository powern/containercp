#include "access/SftpApiRequestParser.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace containercp::access {

const char* kErrorUserNotFound = "sftp_user_not_found";
const char* kErrorUserInvalid = "sftp_user_invalid";
const char* kErrorUserDuplicate = "sftp_user_duplicate";
const char* kErrorUserProvisionFailed = "sftp_user_provision_failed";
const char* kErrorKeyInvalid = "sftp_key_invalid";
const char* kErrorKeyDuplicate = "sftp_key_duplicate";
const char* kErrorKeyNotFound = "sftp_key_not_found";
const char* kErrorKeySyncFailed = "sftp_key_sync_failed";
const char* kErrorGrantNotFound = "sftp_grant_not_found";
const char* kErrorGrantInvalid = "sftp_grant_invalid";
const char* kErrorGrantConflict = "sftp_grant_conflict";
const char* kErrorGrantApplyFailed = "sftp_grant_apply_failed";
const char* kErrorGrantRevokeFailed = "sftp_grant_revoke_failed";
const char* kErrorSiteNotFound = "sftp_site_not_found";
const char* kErrorBackendUnavailable = "sftp_backend_unavailable";
const char* kErrorBackendFailure = "sftp_backend_failure";
const char* kErrorReconciliationBusy = "sftp_reconciliation_busy";
const char* kErrorJsonInvalid = "sftp_json_invalid";

namespace {

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Extract a JSON string value for a given key.
// Returns empty string if key not found.
// Does NOT handle escaped quotes inside the value (project limitation).
std::string extract_string(const std::string& body, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = body.find(search);
    if (pos == std::string::npos) {
        // Try with space after colon
        search = "\"" + key + "\": \"";
        pos = body.find(search);
        if (pos == std::string::npos) return "";
    }
    auto start = body.find('"', pos + search.size() - 1);
    if (start == std::string::npos) return "";
    auto end = body.find('"', start + 1);
    if (end == std::string::npos) return "";
    return body.substr(start + 1, end - start - 1);
}

// Check if a boolean key is present and parse it.
// Returns false and sets present=false if key is missing.
// Returns false and sets present=true if value is not true/false.
bool extract_bool(const std::string& body, const std::string& key, bool& out, bool& present) {
    std::string search = "\"" + key + "\":";
    auto p = body.find(search);
    if (p == std::string::npos) { present = false; out = false; return true; }
    present = true;
    p += search.size();
    while (p < body.size() && is_whitespace(body[p])) ++p;
    if (p >= body.size()) return false;
    if (body.substr(p, 4) == "true") { out = true; return true; }
    if (body.substr(p, 5) == "false") { out = false; return true; }
    return false;
}

// Extract an integer value.
bool extract_int(const std::string& body, const std::string& key, int& out, bool& present) {
    std::string search = "\"" + key + "\":";
    auto p = body.find(search);
    if (p == std::string::npos) { present = false; out = 0; return true; }
    present = true;
    p += search.size();
    while (p < body.size() && is_whitespace(body[p])) ++p;
    if (p >= body.size()) return false;
    char* e = nullptr;
    long v = std::strtol(body.c_str() + p, &e, 10);
    if (e == body.c_str() + p) return false;
    out = static_cast<int>(v);
    return true;
}

// Track seen fields to reject duplicates
struct FieldTracker {
    std::vector<std::string> fields;
    bool add(const std::string& f) {
        if (std::find(fields.begin(), fields.end(), f) != fields.end()) return false;
        fields.push_back(f);
        return true;
    }
};

} // namespace

SftpParsedResult parse_create_user_body(const std::string& body, size_t max_size) {
    SftpParsedResult r;
    if (body.size() > max_size) { r.error_code = kErrorJsonInvalid; r.error_details = "body too large"; return r; }
    if (body.empty() || body[0] != '{') { r.error_code = kErrorJsonInvalid; r.error_details = "not valid JSON"; return r; }

    FieldTracker ft;
    std::string un = extract_string(body, "username");
    if (un.empty()) { r.error_code = kErrorUserInvalid; r.error_details = "username required"; return r; }
    if (!ft.add("username")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: username"; return r; }
    if (un.size() > 64) { r.error_code = kErrorUserInvalid; r.error_details = "username too long"; return r; }

    bool en_present = false;
    if (!extract_bool(body, "enabled", r.create_user.enabled, en_present)) {
        r.error_code = kErrorJsonInvalid; r.error_details = "enabled must be boolean"; return r;
    }
    if (en_present && !ft.add("enabled")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: enabled"; return r; }
    r.create_user.enabled_present = en_present;

    // Reject unknown fields for this context
    for (const auto& known : {"permission", "publicKey", "comment", "siteId", "site_id"}) {
        if (body.find("\"" + std::string(known) + "\":") != std::string::npos) {
            r.error_code = kErrorJsonInvalid; r.error_details = std::string("unknown field: ") + known; return r;
        }
    }

    r.create_user.username = un;
    r.valid = true;
    return r;
}

SftpParsedResult parse_patch_user_body(const std::string& body, size_t max_size) {
    SftpParsedResult r;
    if (body.size() > max_size) { r.error_code = kErrorJsonInvalid; r.error_details = "body too large"; return r; }
    if (body.empty() || body[0] != '{') { r.error_code = kErrorJsonInvalid; r.error_details = "not valid JSON"; return r; }

    FieldTracker ft;
    bool en_present = false;
    if (!extract_bool(body, "enabled", r.patch_user.enabled, en_present)) {
        r.error_code = kErrorJsonInvalid; r.error_details = "enabled must be boolean"; return r;
    }
    if (!en_present) { r.error_code = kErrorUserInvalid; r.error_details = "enabled field required"; return r; }
    if (!ft.add("enabled")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: enabled"; return r; }

    r.patch_user.enabled_present = true;
    r.valid = true;
    return r;
}

SftpParsedResult parse_create_key_body(const std::string& body, size_t max_size) {
    SftpParsedResult r;
    if (body.size() > max_size) { r.error_code = kErrorJsonInvalid; r.error_details = "body too large"; return r; }
    if (body.empty() || body[0] != '{') { r.error_code = kErrorJsonInvalid; r.error_details = "not valid JSON"; return r; }

    FieldTracker ft;
    r.create_key.public_key = extract_string(body, "publicKey");
    if (r.create_key.public_key.empty()) {
        // Check for the key with hyphen removed
        r.create_key.public_key = extract_string(body, "public_key");
    }
    if (r.create_key.public_key.empty()) {
        r.error_code = kErrorKeyInvalid; r.error_details = "publicKey required"; return r;
    }
    if (!ft.add("publicKey")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: publicKey"; return r; }

    r.create_key.comment = extract_string(body, "comment");
    if (ft.add("comment")) { /* ok */ } else { r.create_key.comment.clear(); }

    bool en_present = false;
    if (!extract_bool(body, "enabled", r.create_key.enabled, en_present)) {
        r.error_code = kErrorJsonInvalid; r.error_details = "enabled must be boolean"; return r;
    }
    if (en_present && !ft.add("enabled")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: enabled"; return r; }
    r.create_key.enabled_present = en_present;

    r.valid = true;
    return r;
}

SftpParsedResult parse_create_grant_body(const std::string& body, size_t max_size) {
    SftpParsedResult r;
    if (body.size() > max_size) { r.error_code = kErrorJsonInvalid; r.error_details = "body too large"; return r; }
    if (body.empty() || body[0] != '{') { r.error_code = kErrorJsonInvalid; r.error_details = "not valid JSON"; return r; }

    FieldTracker ft;
    std::string perm = extract_string(body, "permission");
    if (perm.empty()) { r.error_code = kErrorGrantInvalid; r.error_details = "permission required"; return r; }
    if (!ft.add("permission")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: permission"; return r; }
    if (perm == "ro") perm = "read_only";
    else if (perm == "rw") perm = "read_write";
    if (perm != "read_only" && perm != "read_write" && perm != "deploy") {
        r.error_code = kErrorGrantInvalid; r.error_details = "permission must be ro or rw"; return r;
    }
    r.create_grant.permission = perm;

    int sid = 0; bool sid_present = false;
    if (!extract_int(body, "siteId", sid, sid_present)) {
        r.error_code = kErrorJsonInvalid; r.error_details = "siteId must be integer"; return r;
    }
    if (!sid_present) { r.error_code = kErrorGrantInvalid; r.error_details = "siteId required"; return r; }
    if (!ft.add("siteId")) { r.error_code = kErrorJsonInvalid; r.error_details = "duplicate field: siteId"; return r; }
    r.create_grant.site_id = static_cast<uint64_t>(sid);
    r.create_grant.site_id_present = true;

    r.valid = true;
    return r;
}

} // namespace containercp::access
