cmake_minimum_required(VERSION 3.25)

project(ContainerCP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(containercp
    app/containercp/main.cpp
    libs/cli/CommandDispatcher.cpp
    libs/config/Config.cpp
    libs/core/Application.cpp
    libs/core/Resource.cpp
    libs/core/ResourceManager.cpp
    libs/backup/Backup.cpp
    libs/backup/BackupManager.cpp
    libs/core/ServiceRegistry.cpp
    libs/database/Database.cpp
    libs/database/DatabaseManager.cpp
    libs/domain/Domain.cpp
    libs/domain/DomainManager.cpp
    libs/docker/ComposeGenerator.cpp
    libs/docker/EnvGenerator.cpp
    libs/filesystem/Filesystem.cpp
    libs/filesystem/SiteLayout.cpp
    libs/logger/Logger.cpp
    libs/node/Node.cpp
    libs/operations/SiteCreateOperation.cpp
    libs/php/PhpVersion.cpp
    libs/php/PhpVersionManager.cpp
    libs/provider/DockerComposeProvider.cpp
    libs/runtime/DockerRuntime.cpp
    libs/site/Site.cpp
    libs/site/SiteManager.cpp
    libs/storage/Storage.cpp
    libs/template/TemplateEngine.cpp
    libs/user/User.cpp
    libs/user/UserManager.cpp
)

target_include_directories(containercp PRIVATE
    libs
)

target_compile_options(containercp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
