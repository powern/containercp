add_executable(containercp_tests
    main.cpp
    test_validator.cpp
    test_managers.cpp
    test_access.cpp
    test_proxy.cpp
    test_ssl.cpp
    test_cert_store.cpp
    test_storage.cpp
    test_api.cpp
    test_daemon.cpp
    test_template.cpp
    test_runtime.cpp
    test_runtime_sync.cpp
    test_health.cpp
    test_backup.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/Validator.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/StringUtils.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/PasswordGenerator.cpp
    ${CMAKE_SOURCE_DIR}/libs/user/User.cpp
    ${CMAKE_SOURCE_DIR}/libs/user/UserManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/site/Site.cpp
    ${CMAKE_SOURCE_DIR}/libs/site/SiteManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/domain/Domain.cpp
    ${CMAKE_SOURCE_DIR}/libs/domain/DomainManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/domain/DomainViewService.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessUser.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessUserManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessGrant.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessGrantManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/proxy/ProxyConfigBuilder.cpp
    ${CMAKE_SOURCE_DIR}/libs/proxy/ReverseProxy.cpp
    ${CMAKE_SOURCE_DIR}/libs/proxy/ReverseProxyManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/SslCertificate.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/SslCertificateManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/AcmeClient.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/CertificateStore.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/PemCertificateProvider.cpp
    ${CMAKE_SOURCE_DIR}/libs/ssl/HTTP01ChallengeProvider.cpp
    ${CMAKE_SOURCE_DIR}/libs/storage/Storage.cpp
    ${CMAKE_SOURCE_DIR}/libs/node/Node.cpp
    ${CMAKE_SOURCE_DIR}/libs/core/Resource.cpp
    ${CMAKE_SOURCE_DIR}/libs/api/Router.cpp
    ${CMAKE_SOURCE_DIR}/libs/api/Response.cpp
    ${CMAKE_SOURCE_DIR}/libs/api/JsonFormatter.cpp
    ${CMAKE_SOURCE_DIR}/libs/daemon/CommandProtocol.cpp
    ${CMAKE_SOURCE_DIR}/libs/template/TemplateEngine.cpp
    ${CMAKE_SOURCE_DIR}/libs/profile/ProfileManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/backup/Backup.cpp
    ${CMAKE_SOURCE_DIR}/libs/backup/BackupManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/backup/TarBackupProvider.cpp
    ${CMAKE_SOURCE_DIR}/libs/logger/Logger.cpp
    ${CMAKE_SOURCE_DIR}/libs/jobs/JobManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/jobs/JobExecutor.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/CommandExecutor.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/RuntimeActionExecutor.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/RuntimeSynchronizer.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/HealthReport.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/ServiceRole.cpp
    ${CMAKE_SOURCE_DIR}/libs/runtime/SiteRuntimeManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailAliasManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/DkimManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailDomain.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailModuleState.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailDomainManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailPasswordHasher.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/MailboxManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/mail/providers/DockerMailProvider.cpp
)

target_include_directories(containercp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/libs
)

target_compile_options(containercp_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(containercp_tests PRIVATE
    curl
    ssl
    crypto
    crypt
)

add_test(NAME containercp_tests COMMAND containercp_tests)
