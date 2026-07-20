#include "WordPressConfigUpdater.h"

#include "wordpress/WordPressConfigDetector.h"
#include "wordpress/WordPressPhpDefineScanner.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace containercp::wordpress {
namespace {

namespace fs = std::filesystem;

struct ReplacementSpan {
    std::size_t value_start = 0;
    std::size_t value_end = 0;
    char quote = '\'';
    bool conditional = false;
};

std::vector<ReplacementSpan> find_target_spans(const std::string& content, const std::string& target_name) {
    std::vector<ReplacementSpan> spans;
    for (const auto& call : find_php_define_calls(content)) {
        auto args = split_php_top_level_arguments(call.body, call.body_start);
        if (args.size() >= 2) {
            auto constant_name = parse_php_string_literal(args[0]);
            if (constant_name && *constant_name == target_name) {
                auto span = php_literal_value_span(args[1]);
                if (span) {
                    spans.push_back({span->value_start, span->value_end, span->quote, call.conditional});
                } else {
                    spans.push_back({0, 0, '\'', call.conditional});
                }
            }
        }
    }
    return spans;
}

std::optional<std::string> encode_php_literal_value(const std::string& value, char quote) {
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char c : value) {
        if (quote == '\'') {
            if (c == '\\' || c == '\'') {
                encoded.push_back('\\');
                encoded.push_back(static_cast<char>(c));
            } else if (c == '\n' || c == '\r' || c == '\t') {
                encoded.push_back(static_cast<char>(c));
            } else if (c < 0x20 || c == 0x7f) {
                return std::nullopt;
            } else {
                encoded.push_back(static_cast<char>(c));
            }
            continue;
        }

        switch (c) {
        case '\\':
            encoded += "\\\\";
            break;
        case '"':
            encoded += "\\\"";
            break;
        case '$':
            encoded += "\\$";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        case '\0':
            encoded += "\\0";
            break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buffer[5] = {'\\', 'x', '0', '0', '\0'};
                std::snprintf(buffer, sizeof(buffer), "\\x%02X", c);
                encoded += buffer;
            } else {
                encoded.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return encoded;
}

WordPressConfigUpdateResult failure(std::string code, std::string message) {
    WordPressConfigUpdateResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

WordPressConfigFileUpdateResult file_failure(std::string code, std::string message) {
    WordPressConfigFileUpdateResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_all(int fd, const std::string& content) {
    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

void fsync_parent_dir(const fs::path& path) {
    const int dir_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) {
        (void)::fsync(dir_fd);
        (void)::close(dir_fd);
    }
}

fs::path make_temp_path(const fs::path& config_path) {
    static unsigned long counter = 0;
    ++counter;
    return config_path.parent_path() /
           ("." + config_path.filename().string() + ".containercp-tmp-" + std::to_string(::getpid()) + "-" +
            std::to_string(counter));
}

bool write_atomic_preserving_metadata(const fs::path& config_path,
                                      const std::string& content,
                                      mode_t mode,
                                      uid_t uid,
                                      gid_t gid,
                                      std::string& error_code) {
    const fs::path temp_path = make_temp_path(config_path);
    const int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        error_code = "temp_create_failed";
        return false;
    }

    bool ok = write_all(fd, content);
    if (ok && ::fchmod(fd, mode) != 0) {
        ok = false;
        error_code = "metadata_preserve_failed";
    }
    if (ok && ::geteuid() == 0 && ::fchown(fd, uid, gid) != 0) {
        ok = false;
        error_code = "metadata_preserve_failed";
    }
    if (ok && ::fsync(fd) != 0) {
        ok = false;
        error_code = "temp_sync_failed";
    }
    if (::close(fd) != 0 && ok) {
        ok = false;
        error_code = "temp_close_failed";
    }
    if (!ok) {
        (void)::unlink(temp_path.c_str());
        if (error_code.empty()) {
            error_code = "temp_write_failed";
        }
        return false;
    }

    struct stat before_rename {};
    if (::lstat(config_path.c_str(), &before_rename) != 0 || !S_ISREG(before_rename.st_mode) || S_ISLNK(before_rename.st_mode)) {
        (void)::unlink(temp_path.c_str());
        error_code = "unsafe_path";
        return false;
    }

    if (::rename(temp_path.c_str(), config_path.c_str()) != 0) {
        (void)::unlink(temp_path.c_str());
        error_code = "atomic_rename_failed";
        return false;
    }
    fsync_parent_dir(config_path);
    return true;
}

} // namespace

std::string wordpress_update_field_name(WordPressConfigUpdateField field) {
    switch (field) {
    case WordPressConfigUpdateField::DbName:
        return "DB_NAME";
    case WordPressConfigUpdateField::DbUser:
        return "DB_USER";
    case WordPressConfigUpdateField::DbPassword:
        return "DB_PASSWORD";
    case WordPressConfigUpdateField::DbHost:
        return "DB_HOST";
    }
    return "DB_PASSWORD";
}

WordPressConfigUpdateResult WordPressConfigUpdater::render_update(const std::string& content,
                                                                  WordPressConfigUpdateField field,
                                                                  const std::string& new_value) const {
    const std::string target_name = wordpress_update_field_name(field);
    const auto spans = find_target_spans(content, target_name);
    if (spans.empty()) {
        return failure("unsupported_credential_source", target_name + " is missing or is not a direct string literal");
    }
    if (spans.size() != 1) {
        return failure("ambiguous_credential_source", target_name + " must be defined exactly once");
    }
    if (spans[0].conditional) {
        return failure("ambiguous_credential_source", target_name + " must not be defined inside a conditional block");
    }
    if (spans[0].value_start == 0 && spans[0].value_end == 0) {
        return failure("unsupported_credential_source", target_name + " is not a direct string literal");
    }

    auto encoded_value = encode_php_literal_value(new_value, spans[0].quote);
    if (!encoded_value) {
        return failure("unsupported_credential_value", target_name + " value cannot be safely represented in existing PHP quote style");
    }

    WordPressConfigUpdateResult result;
    result.success = true;
    result.code = "ok";
    result.message = "WordPress credential rendered";
    result.content = content.substr(0, spans[0].value_start) + *encoded_value + content.substr(spans[0].value_end);
    return result;
}

WordPressConfigFileUpdateResult WordPressConfigUpdater::update_file_atomic(const std::filesystem::path& site_root,
                                                                          const std::filesystem::path& config_path,
                                                                          WordPressConfigUpdateField field,
                                                                          const std::string& new_value) const {
    return update_file_atomic(site_root, config_path, std::vector<WordPressConfigFieldUpdate>{{field, new_value}});
}

WordPressConfigFileUpdateResult WordPressConfigUpdater::update_file_atomic(
    const std::filesystem::path& site_root,
    const std::filesystem::path& config_path,
    const std::vector<WordPressConfigFieldUpdate>& updates) const {
    if (updates.empty()) {
        return file_failure("updates_missing", "At least one WordPress credential update is required");
    }

    WordPressConfigDetector detector;
    const auto safety = detector.inspect_config_path(site_root, config_path);
    if (!safety.safe) {
        return file_failure(safety.code, safety.message);
    }

    struct stat metadata {};
    if (::lstat(safety.config_path.c_str(), &metadata) != 0 || !S_ISREG(metadata.st_mode) || S_ISLNK(metadata.st_mode)) {
        return file_failure("unsafe_path", "WordPress config path is not a regular file");
    }

    auto current_content = read_file(safety.config_path);
    if (!current_content) {
        return file_failure("config_read_failed", "WordPress config file could not be read");
    }

    std::string rendered_content = *current_content;
    for (const auto& update : updates) {
        const auto rendered = render_update(rendered_content, update.field, update.value);
        if (!rendered.success) {
            return file_failure(rendered.code, rendered.message);
        }
        rendered_content = rendered.content;
    }

    std::string error_code;
    if (!write_atomic_preserving_metadata(safety.config_path,
                                          rendered_content,
                                          metadata.st_mode & 07777,
                                          metadata.st_uid,
                                          metadata.st_gid,
                                          error_code)) {
        return file_failure(error_code, "WordPress config file could not be updated atomically");
    }

    WordPressConfigFileUpdateResult result;
    result.success = true;
    result.code = "ok";
    result.message = "WordPress config file updated atomically";
    result.rollback.valid = true;
    result.rollback.site_root = safety.site_root;
    result.rollback.config_path = safety.config_path;
    result.rollback.previous_content = *current_content;
    result.rollback.mode = static_cast<unsigned int>(metadata.st_mode & 07777);
    result.rollback.uid = static_cast<unsigned int>(metadata.st_uid);
    result.rollback.gid = static_cast<unsigned int>(metadata.st_gid);
    return result;
}

WordPressConfigFileUpdateResult WordPressConfigUpdater::update_file_atomic_validated(
    const std::filesystem::path& site_root,
    const std::filesystem::path& config_path,
    WordPressConfigUpdateField field,
    const std::string& new_value,
    const WordPressConfigValidator& validator) const {
    return update_file_atomic_validated(site_root,
                                        config_path,
                                        std::vector<WordPressConfigFieldUpdate>{{field, new_value}},
                                        validator);
}

WordPressConfigFileUpdateResult WordPressConfigUpdater::update_file_atomic_validated(
    const std::filesystem::path& site_root,
    const std::filesystem::path& config_path,
    const std::vector<WordPressConfigFieldUpdate>& updates,
    const WordPressConfigValidator& validator) const {
    if (!validator) {
        return file_failure("validator_missing", "WordPress config validation boundary is required");
    }

    auto update = update_file_atomic(site_root, config_path, updates);
    if (!update.success) {
        return update;
    }

    const auto validation = validator(update.rollback.config_path);
    if (validation.success) {
        return update;
    }

    const auto rollback = rollback_file(update.rollback);
    if (!rollback.success) {
        return file_failure("validation_failed_rollback_failed",
                            "WordPress config validation failed and automatic rollback did not complete");
    }

    return file_failure("syntax_validation_failed", "WordPress config validation failed and rollback completed");
}

WordPressConfigFileUpdateResult WordPressConfigUpdater::rollback_file(const WordPressConfigRollbackHandle& rollback) const {
    if (!rollback.valid) {
        return file_failure("rollback_invalid", "Rollback handle is invalid");
    }

    WordPressConfigDetector detector;
    const auto safety = detector.inspect_config_path(rollback.site_root, rollback.config_path);
    if (!safety.safe) {
        return file_failure(safety.code, safety.message);
    }

    std::string error_code;
    if (!write_atomic_preserving_metadata(safety.config_path,
                                          rollback.previous_content,
                                          static_cast<mode_t>(rollback.mode),
                                          static_cast<uid_t>(rollback.uid),
                                          static_cast<gid_t>(rollback.gid),
                                          error_code)) {
        return file_failure(error_code, "WordPress config file could not be rolled back atomically");
    }

    WordPressConfigFileUpdateResult result;
    result.success = true;
    result.code = "ok";
    result.message = "WordPress config file rolled back atomically";
    return result;
}

} // namespace containercp::wordpress
