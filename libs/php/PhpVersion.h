#ifndef CONTAINERCP_PHP_PHP_VERSION_H
#define CONTAINERCP_PHP_PHP_VERSION_H

#include "core/Resource.h"

#include <string>

namespace containercp::php {

struct PhpVersion : core::Resource {
    std::string version;
    std::string image;
    bool enabled = true;
    bool default_version = false;
};

} // namespace containercp::php

#endif // CONTAINERCP_PHP_PHP_VERSION_H
