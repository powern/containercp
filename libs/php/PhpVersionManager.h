#ifndef CONTAINERCP_PHP_PHP_VERSION_MANAGER_H
#define CONTAINERCP_PHP_PHP_VERSION_MANAGER_H

#include "php/PhpVersion.h"

#include <string>
#include <vector>

namespace containercp::php {

class PhpVersionManager {
public:
    uint64_t create(const std::string& version, const std::string& image, bool default_version);
    PhpVersion* find(const std::string& version);
    PhpVersion* find(uint64_t id);
    PhpVersion* get_default();
    const std::vector<PhpVersion>& list() const;

    void set_versions(const std::vector<PhpVersion>& versions);

private:
    std::vector<PhpVersion> versions_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::php

#endif // CONTAINERCP_PHP_PHP_VERSION_MANAGER_H
