#include "ssl/AcmeClient.h"
#include "logger/Logger.h"

#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    atexit(curl_global_cleanup);

    auto& log = containercp::logger::Logger::instance();

    ::mkdir("/srv/containercp/ssl", 0700);

    containercp::ssl::AcmeClient client(log);

    fprintf(stderr, "=== Step 1: discover_directory ===\n");
    auto dir_result = client.discover_directory();
    fprintf(stderr, "discover_directory: success=%d msg=%s\n",
            dir_result.success, dir_result.message.c_str());

    if (!dir_result.success) {
        fprintf(stderr, "FAIL: directory discovery failed (might be network)\n");
        return 1;
    }

    fprintf(stderr, "=== Step 2: load_or_create_account ===\n");
    std::string key_path = "/srv/containercp/ssl/account_test.pem";
    auto acct_result = client.load_or_create_account(key_path);
    fprintf(stderr, "load_or_create_account: success=%d msg=%s\n",
            acct_result.success, acct_result.message.c_str());

    fprintf(stderr, "=== DONE (no crash) ===\n");
    return 0;
}
