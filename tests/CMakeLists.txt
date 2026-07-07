add_executable(containercp_tests
    main.cpp
    test_validator.cpp
    test_managers.cpp
    test_access.cpp
    test_proxy.cpp
    test_storage.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/Validator.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/StringUtils.cpp
    ${CMAKE_SOURCE_DIR}/libs/utils/PasswordGenerator.cpp
    ${CMAKE_SOURCE_DIR}/libs/user/User.cpp
    ${CMAKE_SOURCE_DIR}/libs/user/UserManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/site/Site.cpp
    ${CMAKE_SOURCE_DIR}/libs/site/SiteManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/domain/Domain.cpp
    ${CMAKE_SOURCE_DIR}/libs/domain/DomainManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessUser.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessUserManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessGrant.cpp
    ${CMAKE_SOURCE_DIR}/libs/access/AccessGrantManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/proxy/ReverseProxy.cpp
    ${CMAKE_SOURCE_DIR}/libs/proxy/ReverseProxyManager.cpp
    ${CMAKE_SOURCE_DIR}/libs/storage/Storage.cpp
    ${CMAKE_SOURCE_DIR}/libs/node/Node.cpp
    ${CMAKE_SOURCE_DIR}/libs/core/Resource.cpp
)

target_include_directories(containercp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/libs
)

target_compile_options(containercp_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(NAME containercp_tests COMMAND containercp_tests)
