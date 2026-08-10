#include "access/SftpKeyService.h"

#include "access/SftpApiRequestParser.h"
#include "access/SshKeyValidator.h"
#include "storage/Storage.h"

#include <exception>

namespace containercp::access {

namespace {

SftpKeyOperationResult failure(const std::string& code, const std::string& details) {
    SftpKeyOperationResult result;
    result.error_code = code;
    result.error_details = details;
    return result;
}

} // namespace

SftpKeyService::SftpKeyService(AccessKeyManager& keys, storage::Storage& storage,
                               LocalSftpProvider& provider, SshKeyGenerator& generator)
    : keys_(keys), storage_(storage), provider_(provider), generator_(generator) {}

SftpKeyOperationResult SftpKeyService::import_key(uint64_t access_user_id,
                                                  const std::string& public_key,
                                                  const std::string& comment,
                                                  bool enabled) {
    auto validation = SshKeyValidator::validate(public_key);
    if (!validation.valid) {
        return failure(kErrorKeyInvalid, validation.error);
    }
    return add_validated_key(access_user_id, validation, comment, enabled, public_key, "");
}

SftpKeyOperationResult SftpKeyService::generate_key(uint64_t access_user_id,
                                                    const std::string& type,
                                                    const std::string& comment,
                                                    bool enabled) {
    auto generated = generator_.generate(type, comment);
    if (!generated.success) {
        return failure(kErrorKeyInvalid, generated.error);
    }
    auto validation = SshKeyValidator::validate(generated.public_key);
    if (!validation.valid) {
        return failure(kErrorKeyInvalid, validation.error);
    }
    return add_validated_key(access_user_id, validation, comment, enabled,
                             generated.public_key, generated.private_key);
}

SftpKeyOperationResult SftpKeyService::add_validated_key(
    uint64_t access_user_id, const SshKeyValidation& validation,
    const std::string& comment, bool enabled,
    const std::string& public_key, const std::string& private_key) {
    for (const auto& existing : keys_.list()) {
        if (existing.access_user_id == access_user_id &&
            existing.fingerprint == validation.fingerprint) {
            return failure(kErrorKeyDuplicate, "");
        }
    }

    const auto previous = keys_.list();
    AccessKey key;
    key.access_user_id = access_user_id;
    key.key_type = validation.key_type;
    key.key_data = validation.key_data;
    key.key_comment = comment.empty() ? validation.key_comment : comment;
    key.fingerprint = validation.fingerprint;
    key.enabled = enabled;

    const auto created_id = keys_.create(key);
    if (created_id == 0) {
        return failure("sftp_backend_failure", "key create failed");
    }
    // Persist before touching authorized_keys. Any later failure restores the
    // exact in-memory and persistent key set from before this operation.
    auto rollback = [&]() {
        keys_.set_keys(previous);
        try {
            storage_.save_access_keys(previous);
        } catch (const std::exception&) {
            // The original operation already failed; do not expose storage internals.
        }
    };
    try {
        storage_.save_access_keys(keys_.list());
    } catch (const std::exception&) {
        rollback();
        return failure("sftp_backend_failure", "key persistence failed");
    }

    auto written = provider_.write_authorized_keys(access_user_id);
    if (!written.success) {
        rollback();
        return failure(kErrorKeySyncFailed, written.message);
    }

    SftpKeyOperationResult result;
    result.success = true;
    result.id = created_id;
    result.key_type = key.key_type;
    result.fingerprint = key.fingerprint;
    result.comment = key.key_comment;
    result.enabled = key.enabled;
    result.public_key = public_key;
    result.private_key = private_key;
    return result;
}

} // namespace containercp::access
