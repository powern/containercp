#include "ComposeGenerator.h"

namespace containercp::docker {

ComposeGenerator::ComposeGenerator(filesystem::Filesystem& fs, const std::string& template_dir)
    : fs_(fs)
    , template_dir_(template_dir)
    , default_template_(
        "services:\n"
        "  web:\n"
        "    image: {{WEB_SERVER_IMAGE}}\n"
        "    container_name: site-{{SITE_ID}}-web\n"
        "    restart: unless-stopped\n"
        "{{WEB_SERVER_CMD}}"
        "    volumes:\n"
        "      - ./public:{{WEB_DOC_ROOT}}\n"
        "      - ./{{WEB_LOCAL_LOG}}:{{WEB_LOG_DIR}}\n"
        "      - ./{{WEB_LOCAL_CONFIG}}:{{WEB_CONFIG_DIR}}\n"
        "    networks:\n"
        "      - containercp-public\n"
        "      - containercp-site-{{SITE_ID}}\n"
        "    depends_on:\n"
        "      php:\n"
        "        condition: service_healthy\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"{{WEB_HEALTH_CMD}}\", \"-t\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "    labels:\n"
        "      - \"containercp.domain={{DOMAIN}}\"\n"
        "      - \"containercp.site.id={{SITE_ID}}\"\n"
        "\n"
        "  php:\n"
        "    image: {{PHP_IMAGE}}\n"
        "    container_name: site-{{SITE_ID}}-php\n"
        "    restart: unless-stopped\n"
        "    volumes:\n"
        "      - ./public:{{WEB_DOC_ROOT}}\n"
        "      - ./logs:/var/log/php\n"
        "      - ./tmp:/tmp\n"
        "    networks:\n"
        "      - containercp-site-{{SITE_ID}}\n"
        "{{MAIL_NETWORK}}"
        "    environment:\n"
        "      - SITE_DOMAIN=${SITE_DOMAIN}\n"
        "      - SITE_OWNER=${SITE_OWNER}\n"
        "      - TZ=${TZ}\n"
        "    labels:\n"
        "      - \"containercp.site.id={{SITE_ID}}\"\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"php-fpm\", \"-t\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "\n"
        "  mariadb:\n"
        "    image: mariadb:lts\n"
        "    container_name: site-{{SITE_ID}}-db\n"
        "    restart: unless-stopped\n"
        "    volumes:\n"
        "      - db-data:/var/lib/mysql\n"
        "    networks:\n"
        "      - containercp-site-{{SITE_ID}}\n"
        "    labels:\n"
        "      - \"containercp.site.id={{SITE_ID}}\"\n"
        "    environment:\n"
        "      - MYSQL_DATABASE=${DB_NAME}\n"
        "      - MYSQL_USER=${DB_USER}\n"
        "      - MYSQL_PASSWORD=${DB_PASSWORD}\n"
        "      - MYSQL_ROOT_PASSWORD=${MYSQL_ROOT_PASSWORD}\n"
        "      - TZ=${TZ}\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"healthcheck.sh\", \"--connect\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "\n"
        "  redis:\n"
        "    image: redis:alpine\n"
        "    container_name: site-{{SITE_ID}}-redis\n"
        "    restart: unless-stopped\n"
        "    command: [\"redis-server\", \"--requirepass\", \"${REDIS_PASSWORD}\"]\n"
        "    networks:\n"
        "      - containercp-site-{{SITE_ID}}\n"
        "    labels:\n"
        "      - \"containercp.site.id={{SITE_ID}}\"\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"redis-cli\", \"ping\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "\n"
        "volumes:\n"
        "  db-data:\n"
        "\n"
"networks:\n"
"  containercp-public:\n"
"    external: true\n"
"  containercp-site-{{SITE_ID}}:\n"
"{{MAIL_NETWORK_DEFINITION}}"
    )
{
}

std::string ComposeGenerator::get_or_create_template() {
    std::string template_path = template_dir_ + "php-site.compose.template";
    fs_.create_directory(template_dir_);
    fs_.create_file(template_path, default_template_);
    return fs_.read_file(template_path);
}

bool ComposeGenerator::generate(const std::string& domain, const std::string& owner,
                                 const std::string& php_image, const std::string& output_path,
                                 const std::string& site_id,
                                 const std::string& web_server_image,
                                 const std::string& web_config_dir,
                                 const std::string& web_log_dir,
                                 const std::string& web_doc_root,
                                 const std::string& web_local_config,
                                 const std::string& web_local_log,
                                 const std::string& web_server_cmd,
                                 bool mail_module_active) {
    // Use canonical default_template_ as source of truth (not disk file).
    // The disk copy is synced for diagnostics but never read back.
    get_or_create_template();  // sync to disk for diagnostic viewing only
    std::string template_content = default_template_;
    std::string mail_net = mail_module_active ? "      - containercp-mail\n" : "";
    std::string mail_net_def = mail_module_active
        ? "  containercp-mail:\n    external: true\n"
        : "";
    std::string rendered = engine_.render(template_content, domain, owner, php_image, site_id,
        web_server_image, web_config_dir, web_log_dir, web_doc_root,
        web_local_config, web_local_log, web_server_cmd, mail_net, mail_net_def);
    return fs_.create_file(output_path, rendered);
}

} // namespace containercp::docker
