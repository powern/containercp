#ifndef CONTAINERCP_DOCKER_ENV_GENERATOR_H
#define CONTAINERCP_DOCKER_ENV_GENERATOR_H

#include "filesystem/Filesystem.h"

#include <string>

namespace containercp::docker {

class EnvGenerator {
public:
    EnvGenerator(filesystem::Filesystem& fs, const std::string& site_dir);

    bool generate(const std::string& domain, const std::string& owner);
    bool generate(const std::string& domain, const std::string& owner,
                  const std::string& db_name, const std::string& db_user,
                  const std::string& db_password);

private:
    filesystem::Filesystem& fs_;
    std::string site_dir_;
};

} // namespace containercp::docker

#endif // CONTAINERCP_DOCKER_ENV_GENERATOR_H
