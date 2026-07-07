#include "EnvGenerator.h"
#include "utils/PasswordGenerator.h"

#include <sstream>

namespace containercp::docker {

EnvGenerator::EnvGenerator(filesystem::Filesystem& fs, const std::string& site_dir)
    : fs_(fs)
    , site_dir_(site_dir)
{
}

bool EnvGenerator::generate(const std::string& domain, const std::string& owner) {
    return generate(domain, owner, "site_db", "site_user",
                    utils::PasswordGenerator::generate());
}

bool EnvGenerator::generate(const std::string& domain, const std::string& owner,
                             const std::string& db_name, const std::string& db_user,
                             const std::string& db_password) {
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
    env << "DB_NAME=" << db_name << "\n";
    env << "DB_USER=" << db_user << "\n";
    env << "DB_PASSWORD=" << db_password << "\n";
    env << "MYSQL_ROOT_PASSWORD=" << utils::PasswordGenerator::generate(48) << "\n";
    env << "\n";

    env << "# Redis\n";
    env << "REDIS_PASSWORD=" << utils::PasswordGenerator::generate() << "\n";
    env << "\n";

    env << "# Timezone\n";
    env << "TZ=UTC\n";

    return fs_.create_file(site_dir_ + ".env", env.str());
}

} // namespace containercp::docker
