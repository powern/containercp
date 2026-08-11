#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_CONTEXT_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_CONTEXT_H

#include "config/Config.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressConfigService.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace containercp::wordpress {

// Transient, operation-scoped identity for a managed WordPress runtime.
// Container, image, network, and process identities are never persisted.
struct WordPressRuntimeContext {
    bool ok = false;
    std::string failure_code;
    std::string message;

    uint64_t site_id = 0;
    std::string domain;
    std::filesystem::path site_root;
    std::filesystem::path document_root;
    std::filesystem::path config_path;
    std::filesystem::path compose_file;

    std::string compose_project;
    std::string php_service;
    std::string php_container;
    std::string configured_php_image;
    std::string immutable_php_image_id;
    std::string private_network;
    std::string private_network_id;
    std::string container_document_root;
    int64_t php_fpm_uid = -1;
    int64_t php_fpm_gid = -1;
    bool document_root_mount_read_write = false;

    bool runtime_capable = false;
    bool read_only_capable = false;
    bool mutation_capable = false;
    bool filesystem_read_access_proven = false;
    bool filesystem_mutation_access_proven = false;
};

class WordPressRuntimeContextResolver {
public:
    WordPressRuntimeContextResolver(runtime::CommandExecutor& executor,
                                    site::SiteManager& sites,
                                    WordPressConfigService& config_service,
                                    config::Config& config,
                                    logger::Logger& logger);

    WordPressRuntimeContext resolve(uint64_t site_id) const;

private:
    WordPressRuntimeContext failure(uint64_t site_id,
                                    std::string domain,
                                    std::string code,
                                    std::string message) const;
    bool resolve_php_container(const WordPressRuntimeContext& base,
                               WordPressRuntimeContext& context) const;
    bool resolve_private_network(WordPressRuntimeContext& context) const;
    bool resolve_document_root_mount(WordPressRuntimeContext& context) const;
    bool resolve_php_fpm_identity(WordPressRuntimeContext& context) const;
    bool resolve_filesystem_access(WordPressRuntimeContext& context) const;
    bool verify_image_identity(WordPressRuntimeContext& context) const;

    runtime::CommandExecutor& executor_;
    site::SiteManager& sites_;
    WordPressConfigService& config_service_;
    config::Config& config_;
    logger::Logger& logger_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_CONTEXT_H
