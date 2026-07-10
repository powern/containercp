#include "runtime/RuntimeSynchronizer.h"

#include "doctest/doctest.h"

TEST_CASE("RuntimeSynchronizer register and sync") {
    containercp::runtime::RuntimeSynchronizer sync;

    int call_count = 0;
    sync.register_handler("test", [&call_count]() -> containercp::core::OperationResult {
        call_count++;
        return {true, "ok"};
    });

    auto r1 = sync.sync("test");
    CHECK(r1.success);
    CHECK(call_count == 1);

    auto r2 = sync.sync("test");
    CHECK(r2.success);
    CHECK(call_count == 2);
}

TEST_CASE("RuntimeSynchronizer unknown handler returns error") {
    containercp::runtime::RuntimeSynchronizer sync;

    auto r = sync.sync("nonexistent");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("nonexistent") != std::string::npos);
}

TEST_CASE("RuntimeSynchronizer re-register replaces handler") {
    containercp::runtime::RuntimeSynchronizer sync;

    sync.register_handler("x", []() -> containercp::core::OperationResult {
        return {false, "old"};
    });

    sync.register_handler("x", []() -> containercp::core::OperationResult {
        return {true, "new"};
    });

    auto r = sync.sync("x");
    CHECK(r.success);
    CHECK(r.message == "new");
}

TEST_CASE("RuntimeSynchronizer handler failure propagates") {
    containercp::runtime::RuntimeSynchronizer sync;

    sync.register_handler("fail", []() -> containercp::core::OperationResult {
        return {false, "something broke"};
    });

    auto r = sync.sync("fail");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("something broke") != std::string::npos);
}

TEST_CASE("RuntimeSynchronizer multiple independent handlers") {
    containercp::runtime::RuntimeSynchronizer sync;

    int mail_count = 0;
    int dns_count = 0;

    sync.register_handler("mail", [&mail_count]() -> containercp::core::OperationResult {
        mail_count++;
        return {true, "mail synced"};
    });

    sync.register_handler("dns", [&dns_count]() -> containercp::core::OperationResult {
        dns_count++;
        return {true, "dns synced"};
    });

    sync.sync("mail");
    CHECK(mail_count == 1);
    CHECK(dns_count == 0);

    sync.sync("dns");
    CHECK(mail_count == 1);
    CHECK(dns_count == 1);

    sync.sync("mail");
    CHECK(mail_count == 2);
}
