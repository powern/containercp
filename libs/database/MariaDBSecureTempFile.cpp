#include "database/MariaDBSecureTempFile.h"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace containercp::database {
namespace fs = std::filesystem;

namespace {

void write_all(int fd, const std::string& content) {
    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("secure temporary credential write failed");
        }
        if (written == 0) {
            throw std::runtime_error("secure temporary credential write was incomplete");
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

} // namespace

MariaDBSecureTempFile::MariaDBSecureTempFile(fs::path directory, fs::path path)
    : directory_(std::move(directory))
    , path_(std::move(path)) {
}

MariaDBSecureTempFile::MariaDBSecureTempFile(MariaDBSecureTempFile&& other) noexcept
    : directory_(std::move(other.directory_))
    , path_(std::move(other.path_)) {
    other.directory_.clear();
    other.path_.clear();
}

MariaDBSecureTempFile& MariaDBSecureTempFile::operator=(MariaDBSecureTempFile&& other) noexcept {
    if (this != &other) {
        cleanup();
        directory_ = std::move(other.directory_);
        path_ = std::move(other.path_);
        other.directory_.clear();
        other.path_.clear();
    }
    return *this;
}

MariaDBSecureTempFile::~MariaDBSecureTempFile() {
    cleanup();
}

bool mariadb_temp_parent_is_safe(const fs::path& path) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return false;
    }
    if (st.st_uid != ::geteuid()) {
        return false;
    }
    return (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

MariaDBSecureTempFile MariaDBSecureTempFile::create(const std::string& prefix,
                                                    const std::string& suffix,
                                                    const std::string& content) {
    fs::path base = fs::temp_directory_path();
    std::string dir_template = (base / (prefix + "-XXXXXX")).string();
    std::vector<char> dir_buffer(dir_template.begin(), dir_template.end());
    dir_buffer.push_back('\0');
    char* created_dir = ::mkdtemp(dir_buffer.data());
    if (created_dir == nullptr) {
        throw std::runtime_error("secure temporary credential directory could not be created");
    }
    fs::path directory = created_dir;
    if (::chmod(directory.c_str(), S_IRWXU) != 0 || !mariadb_temp_parent_is_safe(directory)) {
        (void)::rmdir(directory.c_str());
        throw std::runtime_error("secure temporary credential directory is unsafe");
    }

    fs::path file_path = directory / (prefix + suffix);
    const int fd = ::open(file_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        (void)::rmdir(directory.c_str());
        throw std::runtime_error("secure temporary credential file could not be created");
    }

    try {
        write_all(fd, content);
        if (::fsync(fd) != 0) {
            throw std::runtime_error("secure temporary credential file could not be synced");
        }
        if (::close(fd) != 0) {
            throw std::runtime_error("secure temporary credential file could not be closed");
        }
    } catch (...) {
        (void)::close(fd);
        (void)::unlink(file_path.c_str());
        (void)::rmdir(directory.c_str());
        throw;
    }

    return MariaDBSecureTempFile(directory, file_path);
}

void MariaDBSecureTempFile::cleanup() noexcept {
    if (!path_.empty()) {
        (void)::unlink(path_.c_str());
        path_.clear();
    }
    if (!directory_.empty()) {
        (void)::rmdir(directory_.c_str());
        directory_.clear();
    }
}

} // namespace containercp::database
