#ifndef CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H
#define CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H

#include "filesystem/Filesystem.h"
#include "template/TemplateEngine.h"

#include <string>

namespace containercp::docker {

class ComposeGenerator {
public:
    ComposeGenerator(filesystem::Filesystem& fs, const std::string& template_dir);

    bool generate(const std::string& domain, const std::string& owner, const std::string& output_path);

private:
    std::string get_or_create_template();

    filesystem::Filesystem& fs_;
    template_engine::TemplateEngine engine_;
    std::string template_dir_;
    std::string default_template_;
};

} // namespace containercp::docker

#endif // CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H
