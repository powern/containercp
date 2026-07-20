#include "utils/PathUtils.h"

#include "doctest/doctest.h"

#include <filesystem>

using namespace containercp::utils;

TEST_CASE("PathUtils treats trailing separators as insignificant for prefixes") {
    const std::filesystem::path site = "/srv/containercp/sites/unity.softico.ua";

    CHECK(path_has_prefix(site, "/srv/containercp/sites"));
    CHECK(path_has_prefix(site, "/srv/containercp/sites/"));
    CHECK(normalize_path_for_comparison("/srv/containercp/sites") ==
          normalize_path_for_comparison("/srv/containercp/sites/"));
}

TEST_CASE("PathUtils rejects real traversal outside root") {
    CHECK_FALSE(path_has_prefix("/srv/containercp/sites/../../etc", "/srv/containercp/sites"));
    CHECK_FALSE(path_has_prefix("/srv/containercp/sites/../../etc", "/srv/containercp/sites/"));
    CHECK_FALSE(path_has_prefix("/srv/containercp/sites2/unity.softico.ua", "/srv/containercp/sites"));
}
