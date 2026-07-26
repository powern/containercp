#ifndef CONTAINERCP_ACCESS_SFTP_API_REQUEST_PARSER_H
#define CONTAINERCP_ACCESS_SFTP_API_REQUEST_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::access {

struct SftpCreateUserRequest {
    std::string username;
    bool enabled = true;
    bool enabled_present = false;
};

struct SftpPatchUserRequest {
    bool enabled = false;
    bool enabled_present = false;
};

struct SftpCreateKeyRequest {
    std::string public_key;
    std::string comment;
    bool enabled = true;
    bool enabled_present = false;
};

struct SftpCreateGrantRequest {
    uint64_t site_id = 0;
    bool site_id_present = false;
    std::string permission;
};

struct SftpParsedResult {
    bool valid = false;
    std::string error_code;
    std::string error_details;
    // Parsed fields (only valid when valid==true)
    SftpCreateUserRequest create_user;
    SftpPatchUserRequest patch_user;
    SftpCreateKeyRequest create_key;
    SftpCreateGrantRequest create_grant;
};

// Parse a "create SFTP user" body.
// Returns valid=false with error_code on: malformed JSON, missing username,
// duplicate fields, wrong types, unknown fields, oversized body.
SftpParsedResult parse_create_user_body(const std::string& body, size_t max_size = 65536);

// Parse a "patch SFTP user" body.
SftpParsedResult parse_patch_user_body(const std::string& body, size_t max_size = 65536);

// Parse a "create/update key" body.
SftpParsedResult parse_create_key_body(const std::string& body, size_t max_size = 65536);

// Parse a "create/update grant" body.
SftpParsedResult parse_create_grant_body(const std::string& body, size_t max_size = 65536);

// Stable API error codes
extern const char* kErrorUserNotFound;
extern const char* kErrorUserInvalid;
extern const char* kErrorUserDuplicate;
extern const char* kErrorUserProvisionFailed;
extern const char* kErrorKeyInvalid;
extern const char* kErrorKeyDuplicate;
extern const char* kErrorKeyNotFound;
extern const char* kErrorKeySyncFailed;
extern const char* kErrorGrantNotFound;
extern const char* kErrorGrantInvalid;
extern const char* kErrorGrantConflict;
extern const char* kErrorGrantApplyFailed;
extern const char* kErrorGrantRevokeFailed;
extern const char* kErrorSiteNotFound;
extern const char* kErrorBackendUnavailable;
extern const char* kErrorBackendFailure;
extern const char* kErrorReconciliationBusy;
extern const char* kErrorJsonInvalid;

} // namespace containercp::access

#endif
