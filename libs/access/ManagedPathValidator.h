#ifndef CONTAINERCP_ACCESS_MANAGED_PATH_VALIDATOR_H
#define CONTAINERCP_ACCESS_MANAGED_PATH_VALIDATOR_H

#include <string>

namespace containercp::access {

struct PathValidation {
    bool ok = false;
    std::string error;
    std::string canonical;
};

PathValidation validate_managed_path(const std::string& path, const std::string& managed_root);

} // namespace containercp::access

#endif
