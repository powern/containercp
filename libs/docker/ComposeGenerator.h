#ifndef CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H
#define CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H

#include "filesystem/Filesystem.h"
#include "template/TemplateEngine.h"

#include <string>

namespace containercp::docker {

class ComposeGenerator {
public:
    ComposeGenerator(filesystem::Filesystem& fs, const std::string& template_dir);

    bool generate(const std::string& domain, const std::string& owner,
                  const std::string& php_image, const std::string& output_path,
                  const std::string& site_id = "",
                  const std::string& web_server_image = "nginx:alpine",
                  const std::string& web_config_dir = "/etc/nginx/conf.d",
                  const std::string& web_log_dir = "/var/log/nginx",
                  const std::string& web_doc_root = "/var/www/html",
                  const std::string& web_local_config = "config/nginx",
                  const std::string& web_local_log = "logs/nginx");

private:
    std::string get_or_create_template();

    filesystem::Filesystem& fs_;
    template_engine::TemplateEngine engine_;
    std::string template_dir_;
    std::string default_template_;
};

} // namespace containercp::docker

#endif // CONTAINERCP_DOCKER_COMPOSE_GENERATOR_H
