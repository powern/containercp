#include "SiteLayout.h"

namespace containercp::filesystem {

SiteLayout::SiteLayout(Filesystem& fs, const std::string& site_root)
    : site_root_(site_root)
    , fs_(fs)
{
}

bool SiteLayout::create() {
    static constexpr const char* dirs[] = {
        "www",
        "public",
        "logs/nginx",
        "logs/apache",
        "tmp",
        "ssl",
        "backups",
        "config/nginx",
        "config/apache",
        "config/mariadb/initdb",
        "compose",
    };

    fs_.create_directory(site_root_);

    for (const auto& dir : dirs) {
        std::string path = site_root_ + dir + "/";
        fs_.create_directory(path);
        fs_.create_file(path + ".gitkeep", "");
    }

    fs_.create_file(site_root_ + "README.txt", "This site is managed by ContainerCP.\n");

    return true;
}

} // namespace containercp::filesystem
