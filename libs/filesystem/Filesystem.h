#ifndef CONTAINERCP_FILESYSTEM_FILESYSTEM_H
#define CONTAINERCP_FILESYSTEM_FILESYSTEM_H

#include <string>

namespace containercp::filesystem {

class Filesystem {
public:
    bool create_directory(const std::string& path);
    bool remove_directory(const std::string& path);
    bool exists(const std::string& path);
    bool create_file(const std::string& path, const std::string& content);
    std::string read_file(const std::string& path);
};

} // namespace containercp::filesystem

#endif // CONTAINERCP_FILESYSTEM_FILESYSTEM_H
