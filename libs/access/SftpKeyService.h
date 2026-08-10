#ifndef CONTAINERCP_ACCESS_SFTP_KEY_SERVICE_H
#define CONTAINERCP_ACCESS_SFTP_KEY_SERVICE_H

#include "access/AccessKeyManager.h"
#include "access/LocalSftpProvider.h"
#include "access/SshKeyGenerator.h"
#include "access/SshKeyValidator.h"

#include <cstdint>
#include <string>

namespace containercp::storage { class Storage; }

namespace containercp::access {

struct SftpKeyOperationResult {
    bool success = false;
    std::string error_code;
    std::string error_details;
    uint64_t id = 0;
    std::string key_type;
    std::string fingerprint;
    std::string comment;
    bool enabled = true;
    std::string public_key;
    std::string private_key;
};

class SftpKeyService {
public:
    SftpKeyService(AccessKeyManager& keys, storage::Storage& storage,
                   LocalSftpProvider& provider, SshKeyGenerator& generator);

    SftpKeyOperationResult import_key(uint64_t access_user_id,
                                      const std::string& public_key,
                                      const std::string& comment,
                                      bool enabled);
    SftpKeyOperationResult generate_key(uint64_t access_user_id,
                                        const std::string& type,
                                        const std::string& comment,
                                        bool enabled);

private:
    SftpKeyOperationResult add_validated_key(uint64_t access_user_id,
                                             const SshKeyValidation& validation,
                                             const std::string& comment,
                                             bool enabled,
                                             const std::string& public_key,
                                             const std::string& private_key);

    AccessKeyManager& keys_;
    storage::Storage& storage_;
    LocalSftpProvider& provider_;
    SshKeyGenerator& generator_;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_SFTP_KEY_SERVICE_H
