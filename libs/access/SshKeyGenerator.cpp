#include "access/SshKeyGenerator.h"

#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <vector>

namespace containercp::access {
namespace {

std::string trim(const std::string& value) {
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && (value[start] == ' ' || value[start] == '\t' ||
                           value[start] == '\n' || value[start] == '\r')) {
        ++start;
    }
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\n' || value[end - 1] == '\r')) {
        --end;
    }
    return value.substr(start, end - start);
}

bool read_file(const std::filesystem::path& path, std::string& value) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    value.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

} // namespace

GeneratedSshKey SshKeyGenerator::generate(const std::string& type,
                                          const std::string& comment) const {
    GeneratedSshKey result;
    std::string algorithm;
    if (type == "ed25519") {
        algorithm = "ed25519";
    } else if (type == "rsa") {
        algorithm = "rsa";
    } else {
        result.error = "unsupported key type: " + type;
        return result;
    }

    char template_path[] = "/tmp/containercp-sftp-key-XXXXXX";
    char* directory = ::mkdtemp(template_path);
    if (directory == nullptr) {
        result.error = "temporary key directory creation failed";
        return result;
    }
    const std::filesystem::path directory_path(directory);
    const std::filesystem::path private_path = directory_path / "id";
    const std::filesystem::path public_path = directory_path / "id.pub";

    auto cleanup = [&]() -> bool {
        std::error_code ec;
        std::filesystem::remove_all(directory_path, ec);
        return !ec;
    };

    runtime::CommandExecutor executor;
    std::vector<std::string> args = {
        "/usr/bin/ssh-keygen", "-q", "-t", algorithm, "-N", "",
        "-C", comment, "-f", private_path.string()
    };
    if (algorithm == "rsa") {
        args.insert(args.end() - 2, {"-b", "4096"});
    }
    const auto command = executor.run_safe(args, "", 30, 4096);
    if (command.exit_code != 0) {
        cleanup();
        result.error = "SSH key generation failed";
        return result;
    }

    if (!read_file(public_path, result.public_key) ||
        !read_file(private_path, result.private_key)) {
        cleanup();
        result.public_key.clear();
        result.private_key.clear();
        result.error = "generated SSH key files could not be read";
        return result;
    }
    result.public_key = trim(result.public_key);
    if (!cleanup()) {
        result.public_key.clear();
        result.private_key.clear();
        result.error = "temporary SSH key cleanup failed";
        return result;
    }
    if (result.public_key.empty() || result.private_key.empty()) {
        result.public_key.clear();
        result.private_key.clear();
        result.error = "generated SSH key is empty";
        return result;
    }

    result.success = true;
    return result;
}

} // namespace containercp::access
