#include "EnvGenerator.h"

#include <random>
#include <sstream>

namespace containercp::docker {

EnvGenerator::EnvGenerator(filesystem::Filesystem& fs, const std::string& site_dir)
    : fs_(fs)
    , site_dir_(site_dir)
{
}

std::string EnvGenerator::generate_password(int length) {
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string password;
    password.reserve(length);
    for (int i = 0; i < length; ++i) {
        password += chars[dist(gen)];
    }
    return password;
}

bool EnvGenerator::generate(const std::string& domain, const std::string& owner) {
    std::ostringstream env;

    env << "# Site\n";
    env << "SITE_DOMAIN=" << domain << "\n";
    env << "SITE_NAME=" << domain << "\n";
    env << "SITE_OWNER=" << owner << "\n";
    env << "\n";

    env << "# Nginx\n";
    env << "NGINX_PORT=80\n";
    env << "\n";

    env << "# PHP\n";
    env << "PHP_MEMORY_LIMIT=256M\n";
    env << "PHP_UPLOAD_LIMIT=128M\n";
    env << "\n";

    env << "# MariaDB\n";
    env << "DB_NAME=site_db\n";
    env << "DB_USER=site_user\n";
    env << "DB_PASSWORD=" << generate_password(32) << "\n";
    env << "MYSQL_ROOT_PASSWORD=" << generate_password(48) << "\n";
    env << "\n";

    env << "# Redis\n";
    env << "REDIS_PASSWORD=" << generate_password(32) << "\n";
    env << "\n";

    env << "# Timezone\n";
    env << "TZ=UTC\n";

    return fs_.create_file(site_dir_ + ".env", env.str());
}

} // namespace containercp::docker
