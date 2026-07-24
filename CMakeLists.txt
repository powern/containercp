cmake_minimum_required(VERSION 3.25)

project(ContainerCP VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared library sources (used by both binaries)
set(CONTAINERCP_SOURCES
    libs/access/AccessGrant.cpp
    libs/access/AccessGrantManager.cpp
    libs/access/AccessKey.cpp
    libs/access/AccessKeyManager.cpp
    libs/access/FilesystemPermissionInspector.cpp
    libs/access/MountInspector.cpp
    libs/access/SshKeyValidator.cpp
    libs/access/SystemAccountAllocator.cpp
    libs/access/SystemAccountCommandRunner.cpp
    libs/access/SystemIdentityInspector.cpp
    libs/access/UsernameMapper.cpp
    libs/access/AccessUser.cpp
    libs/access/AccessUserManager.cpp
    libs/access/LocalSftpProvider.cpp
    libs/api/ApiServer.cpp
    libs/api/AuthMiddleware.cpp
    libs/api/WebServer.cpp
    libs/api/JsonFormatter.cpp
    libs/api/Response.cpp
    libs/api/Router.cpp
    libs/auth/AuthService.cpp
    libs/auth/SessionManager.cpp
    libs/auth/AuthUserManager.cpp
    libs/backup/Backup.cpp
    libs/backup/BackupJobService.cpp
    libs/backup/BackupManager.cpp
    libs/backup/BackupService.cpp
    libs/backup/TarBackupProvider.cpp
    libs/config/Config.cpp
    libs/core/Application.cpp
    libs/core/Resource.cpp
    libs/core/ResourceManager.cpp
    libs/core/RecoveryManager.cpp
    libs/core/ServiceRegistry.cpp
    libs/core/StartupManager.cpp
    libs/daemon/CommandProtocol.cpp
    libs/daemon/DaemonApp.cpp
    libs/database/Database.cpp
    libs/database/DatabaseCredentialRotationAdapter.cpp
    libs/database/DatabaseCredentialRotationAudit.cpp
    libs/database/DatabaseCredentialRotationJobService.cpp
    libs/database/DatabaseCredentialRotationService.cpp
    libs/database/DatabaseIdentifierValidator.cpp
    libs/database/DatabaseDumpJobService.cpp
    libs/database/DatabaseDumpService.cpp
    libs/database/DatabaseLifecycleAudit.cpp
    libs/database/DatabaseLifecycleJobService.cpp
    libs/database/DatabaseLifecycleService.cpp
    libs/database/DatabaseViewService.cpp
    libs/database/MariaDBProvider.cpp
    libs/database/MariaDBCredentialProvider.cpp
    libs/database/MariaDBSecureTempFile.cpp
    libs/database/DatabaseManager.cpp
    libs/domain/Domain.cpp
    libs/domain/DomainManager.cpp
    libs/domain/DomainViewService.cpp
    libs/dns/DnsCheckService.cpp
    libs/dns/DnsCheckHandler.cpp
    libs/api/SitesViewService.cpp
    libs/dns/SpfAnalyzer.cpp
    libs/docker/ComposeGenerator.cpp
    libs/docker/EnvGenerator.cpp
    libs/filesystem/Filesystem.cpp
    libs/filesystem/SiteLayout.cpp
    libs/jobs/JobManager.cpp
    libs/jobs/JobExecutor.cpp
    libs/logger/Logger.cpp
    libs/migration/VestaSiteImporter.cpp
    libs/mail/MailAliasManager.cpp
    libs/network/NetworkService.cpp
    libs/mail/DkimManager.cpp
    libs/mail/MailDomain.cpp
    libs/mail/MailModuleState.cpp
    libs/mail/MailDomainManager.cpp
    libs/mail/MailPasswordHasher.cpp
    libs/mail/SiteMailCredentials.cpp
    libs/mail/SiteMailOrchestrator.cpp
    libs/mail/MailboxManager.cpp
    libs/mail/providers/DockerMailProvider.cpp
    libs/node/Node.cpp
    libs/operations/SiteCreateOperation.cpp
    libs/operations/SiteDatabaseVolumeGuard.cpp
    libs/operations/SiteRemoveOperation.cpp
    libs/php/PhpVersion.cpp
    libs/php/PhpVersionManager.cpp
    libs/provider/DockerComposeProvider.cpp
    libs/proxy/NginxProxyProvider.cpp
    libs/proxy/ProxyConfigBuilder.cpp
    libs/proxy/ProxyViewService.cpp
    libs/proxy/ReverseProxy.cpp
    libs/proxy/ReverseProxyManager.cpp
    libs/runtime/DockerRuntime.cpp
    libs/runtime/PortManager.cpp
    libs/runtime/CommandExecutor.cpp
    libs/runtime/RuntimeActionExecutor.cpp
    libs/runtime/RuntimeSynchronizer.cpp
    libs/runtime/HealthReport.cpp
    libs/runtime/ServiceRole.cpp
    libs/runtime/SiteRuntimeManager.cpp
    libs/site/Site.cpp
    libs/site/SiteManager.cpp
    libs/security/PasswordHasher.cpp
    libs/security/SecureRandom.cpp
    libs/sqlconsole/AdminerSqlConsoleProvider.cpp
    libs/sqlconsole/DatabaseSqlConsoleService.cpp
    libs/sqlconsole/SqlConsoleAudit.cpp
    libs/sqlconsole/SqlConsoleSession.cpp
    libs/sqlconsole/SqlConsoleSessionManager.cpp
    libs/sqlconsole/SqlConsoleSessionStore.cpp
    libs/ssl/SslCertificate.cpp
    libs/ssl/SslCertificateManager.cpp
    libs/ssl/LetsEncryptProvider.cpp
    libs/ssl/AcmeClient.cpp
    libs/ssl/CertificateStore.cpp
    libs/ssl/PemCertificateProvider.cpp
    libs/ssl/RenewalScheduler.cpp
    libs/ssl/HTTP01ChallengeProvider.cpp
    libs/storage/Storage.cpp
    libs/storage/SQLiteWrapper.cpp
    libs/storage/ConnectionPool.cpp
    libs/storage/MigrationEngine.cpp
    libs/storage/LegacyImporter.cpp
    libs/storage/LegacyDatasetReader.cpp
    libs/storage/LegacyArchive.cpp
    libs/storage/Verification.cpp
    libs/storage/MigrationOrchestrator.cpp
    libs/storage/SchemaMigrations.cpp
    libs/storage/SQLiteStorage.cpp
    libs/template/TemplateEngine.cpp
    libs/profile/ProfileManager.cpp
    libs/user/User.cpp
    libs/user/UserManager.cpp
    libs/utils/PasswordGenerator.cpp
    libs/utils/PathUtils.cpp
    libs/utils/StringUtils.cpp
    libs/utils/Validator.cpp
    libs/wordpress/WordPressConfigDetector.cpp
    libs/wordpress/WordPressPhpDefineScanner.cpp
    libs/wordpress/WordPressConfigService.cpp
    libs/wordpress/WordPressConfigTypes.cpp
    libs/wordpress/WordPressConfigUpdater.cpp
    libs/wordpress/WordPressDatabaseCredentialResolver.cpp
    libs/wordpress/WordPressRuntimeVerifier.cpp
)

# Daemon binary
add_executable(containercpd
    app/containercpd/main.cpp
    app/containercpd/bootstrap_server.cpp
    ${CONTAINERCP_SOURCES}
)

target_include_directories(containercpd PRIVATE
    libs
)

target_compile_options(containercpd PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

find_package(SQLite3 REQUIRED)

find_path(ARGON2_INCLUDE_DIR argon2.h)
find_library(ARGON2_LIBRARY argon2)
if(ARGON2_INCLUDE_DIR AND ARGON2_LIBRARY)
    message(STATUS "Password hashing backend: Argon2id")
    target_compile_definitions(containercpd PRIVATE CONTAINERCP_HAS_ARGON2)
    target_include_directories(containercpd PRIVATE ${ARGON2_INCLUDE_DIR})
else()
    message(STATUS "Password hashing backend: PBKDF2-SHA256 (OpenSSL); install libargon2-dev for Argon2id")
endif()

target_link_libraries(containercpd PRIVATE
    cares
    curl
    ssl
    crypto
    crypt
    SQLite::SQLite3
)

if(ARGON2_INCLUDE_DIR AND ARGON2_LIBRARY)
    target_link_libraries(containercpd PRIVATE ${ARGON2_LIBRARY})
endif()

# CLI client binary (thin)
add_executable(containercp
    app/containercp/main.cpp
    libs/cli/CommandDispatcher.cpp
    libs/daemon/CommandProtocol.cpp
    libs/daemon/UnixSocketClient.cpp
    libs/utils/Validator.cpp
)

target_include_directories(containercp PRIVATE
    libs
)

target_compile_options(containercp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
