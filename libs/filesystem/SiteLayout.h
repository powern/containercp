#ifndef CONTAINERCP_FILESYSTEM_SITE_LAYOUT_H
#define CONTAINERCP_FILESYSTEM_SITE_LAYOUT_H

#include "filesystem/Filesystem.h"

#include <string>

namespace containercp::filesystem {

class SiteLayout {
public:
    SiteLayout(Filesystem& fs, const std::string& site_root);

    bool create();

private:
    std::string site_root_;
    Filesystem& fs_;
};

} // namespace containercp::filesystem

#endif // CONTAINERCP_FILESYSTEM_SITE_LAYOUT_H
