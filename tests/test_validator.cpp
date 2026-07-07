#include "utils/Validator.h"

#include <string>

#include "doctest/doctest.h"

TEST_CASE("Validator::is_valid_hostname") {
    using containercp::utils::Validator;

    CHECK_FALSE(Validator::is_valid_hostname(""));
    CHECK_FALSE(Validator::is_valid_hostname("a"));
    CHECK(Validator::is_valid_hostname("example.com"));
    CHECK(Validator::is_valid_hostname("sub.domain.local"));
    CHECK_FALSE(Validator::is_valid_hostname("-bad.com"));
    CHECK_FALSE(Validator::is_valid_hostname("bad-.com"));

    std::string long_label = "toolonglabel-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com";
    CHECK_FALSE(Validator::is_valid_hostname(long_label));

    std::string over_253(254, 'a');
    over_253 += ".com";
    CHECK_FALSE(Validator::is_valid_hostname(over_253));
}

TEST_CASE("Validator::is_valid_username") {
    using containercp::utils::Validator;

    CHECK_FALSE(Validator::is_valid_username(""));
    CHECK_FALSE(Validator::is_valid_username("ab"));
    CHECK(Validator::is_valid_username("admin"));
    CHECK(Validator::is_valid_username("john_doe"));
    CHECK_FALSE(Validator::is_valid_username("1admin"));
    CHECK_FALSE(Validator::is_valid_username("-admin"));
    CHECK_FALSE(Validator::is_valid_username("admin-"));
    CHECK_FALSE(Validator::is_valid_username("admin_"));
    CHECK_FALSE(Validator::is_valid_username("BadUser"));
    CHECK_FALSE(Validator::is_valid_username("user name"));
}
