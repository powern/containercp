#include "ComposeGenerator.h"

namespace containercp::docker {

ComposeGenerator::ComposeGenerator(filesystem::Filesystem& fs, const std::string& template_dir)
    : fs_(fs)
    , template_dir_(template_dir)
    , default_template_(
        "services:\n"
        "  nginx:\n"
        "    image: nginx:alpine\n"
        "    container_name: {{DOMAIN}}-nginx\n"
        "    restart: unless-stopped\n"
        "    ports:\n"
        "      - \"${NGINX_PORT}:80\"\n"
        "    volumes:\n"
        "      - ./public:/var/www/html\n"
        "      - ./logs/nginx:/var/log/nginx\n"
        "      - ./config/nginx:/etc/nginx/conf.d\n"
        "    networks:\n"
        "      - site-network\n"
        "    depends_on:\n"
        "      php:\n"
        "        condition: service_healthy\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"nginx\", \"-t\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "    labels:\n"
        "      - \"containercp.domain={{DOMAIN}}\"\n"
        "\n"
        "  php:\n"
        "    image: php:8.4-fpm\n"
        "    container_name: {{DOMAIN}}-php\n"
        "    restart: unless-stopped\n"
        "    volumes:\n"
        "      - ./public:/var/www/html\n"
        "      - ./logs:/var/log/php\n"
        "      - ./tmp:/tmp\n"
        "    networks:\n"
        "      - site-network\n"
        "    environment:\n"
        "      - SITE_DOMAIN=${SITE_DOMAIN}\n"
        "      - SITE_OWNER=${SITE_OWNER}\n"
        "      - TZ=${TZ}\n"
        "    healthcheck:\n"
        "      test: [\"CMD\", \"php-fpm\", \"-t\"]\n"
        "      interval: 30s\n"
        "      timeout: 10s\n"
        "      retries: 3\n"
        "\n"
        "  mariadb:\n"
        "    image: mariadb:lts\n"
        "    container_name: {{DOMAIN}}-db\n"
        "    restart: unless-stopped\n"
        "    volumes:\n"
        "      - db-data:/var/lib/mysql\n"
        "    networks:\n"
        "      - site-network\n"
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
        "    container_name: {{DOMAIN}}-redis\n"
        "    restart: unless-stopped\n"
        "    command: [\"redis-server\", \"--requirepass\", \"${REDIS_PASSWORD}\"]\n"
        "    networks:\n"
        "      - site-network\n"
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
        "  site-network:\n"
    )
{
}

std::string ComposeGenerator::get_or_create_template() {
    std::string template_path = template_dir_ + "php-site.compose.template";

    if (!fs_.exists(template_path)) {
        fs_.create_directory(template_dir_);
        fs_.create_file(template_path, default_template_);
    }

    return fs_.read_file(template_path);
}

bool ComposeGenerator::generate(const std::string& domain, const std::string& owner, const std::string& output_path) {
    std::string template_content = get_or_create_template();
    std::string rendered = engine_.render(template_content, domain, owner);
    return fs_.create_file(output_path, rendered);
}

} // namespace containercp::docker
