#ifndef CONTAINERCP_DOCKER_ENV_GENERATOR_H
#define CONTAINERCP_DOCKER_ENV_GENERATOR_H

#include "filesystem/Filesystem.h"

#include <string>

namespace containercp::docker {

class EnvGenerator {
public:
    EnvGenerator(filesystem::Filesystem& fs, const std::string& site_dir);

    bool generate(const std::string& domain, const std::string& owner);

private:
    std::string generate_password(int length);

    filesystem::Filesystem& fs_;
    std::string site_dir_;
};

} // namespace containercp::docker

#endif // CONTAINERCP_DOCKER_ENV_GENERATOR_H
