#ifndef CONTAINERCP_DATABASE_MARIADB_SECURE_TEMP_FILE_H
#define CONTAINERCP_DATABASE_MARIADB_SECURE_TEMP_FILE_H

#include <filesystem>
#include <string>

namespace containercp::database {

class MariaDBSecureTempFile {
public:
    MariaDBSecureTempFile() = default;
    MariaDBSecureTempFile(const MariaDBSecureTempFile&) = delete;
    MariaDBSecureTempFile& operator=(const MariaDBSecureTempFile&) = delete;
    MariaDBSecureTempFile(MariaDBSecureTempFile&& other) noexcept;
    MariaDBSecureTempFile& operator=(MariaDBSecureTempFile&& other) noexcept;
    ~MariaDBSecureTempFile();

    static MariaDBSecureTempFile create(const std::string& prefix,
                                        const std::string& suffix,
                                        const std::string& content);

    const std::filesystem::path& path() const { return path_; }
    const std::filesystem::path& directory() const { return directory_; }
    bool valid() const { return !path_.empty(); }
    void cleanup() noexcept;

private:
    MariaDBSecureTempFile(std::filesystem::path directory, std::filesystem::path path);

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

bool mariadb_temp_parent_is_safe(const std::filesystem::path& path);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_MARIADB_SECURE_TEMP_FILE_H
