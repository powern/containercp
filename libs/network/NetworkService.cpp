#include "NetworkService.h"

#include <algorithm>
#include <arpa/inet.h>
#include <ares.h>
#include <ares_dns_record.h>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace containercp::network {

namespace {

// Check if a string is a valid IPv4 address
bool is_valid_ipv4(const std::string& s) {
    struct in_addr addr;
    return inet_pton(AF_INET, s.c_str(), &addr) == 1;
}

bool is_valid_ipv6(const std::string& s) {
    struct in6_addr addr;
    return inet_pton(AF_INET6, s.c_str(), &addr) == 1;
}

// Check if IPv4 is in a private/reserved range
bool is_private_ipv4(uint32_t ip) {
    // 127.0.0.0/8 loopback
    if ((ip & 0xFF000000) == 0x7F000000) return true;
    // 10.0.0.0/8
    if ((ip & 0xFF000000) == 0x0A000000) return true;
    // 172.16.0.0/12
    if ((ip & 0xFFF00000) == 0xAC100000) return true;
    // 192.168.0.0/16
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true;
    // 169.254.0.0/16 link-local
    if ((ip & 0xFFFF0000) == 0xA9FE0000) return true;
    // 100.64.0.0/10 CGNAT
    if ((ip & 0xFFC00000) == 0x64400000) return true;
    // 224.0.0.0/4 multicast
    if ((ip & 0xF0000000) == 0xE0000000) return true;
    // 240.0.0.0/4 reserved
    if ((ip & 0xF0000000) == 0xF0000000) return true;
    // 0.0.0.0/8 unspecified
    if ((ip & 0xFF000000) == 0x00000000) return true;
    // 198.18.0.0/15 benchmark
    if ((ip & 0xFFFE0000) == 0xC6120000) return true;
    return false;
}

bool is_link_local_ipv6(const struct in6_addr& addr) {
    // fe80::/10 link-local
    return (addr.s6_addr[0] == 0xFE && (addr.s6_addr[1] & 0xC0) == 0x80);
}

bool is_unique_local_ipv6(const struct in6_addr& addr) {
    // fc00::/7 unique-local
    return (addr.s6_addr[0] & 0xFE) == 0xFC;
}

bool is_loopback_ipv6(const struct in6_addr& addr) {
    // ::1 loopback
    return memcmp(&addr, &in6addr_loopback, sizeof(addr)) == 0;
}

bool is_private_ipv6(const std::string& s) {
    struct in6_addr addr;
    if (inet_pton(AF_INET6, s.c_str(), &addr) != 1) return true; // invalid = treat as private
    return is_loopback_ipv6(addr) || is_link_local_ipv6(addr) || is_unique_local_ipv6(addr);
}

std::string timestamp_now() {
    std::time_t now = std::time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc)) {
        return buf;
    }
    return "";
}

} // anonymous namespace

NetworkService::NetworkService(config::Config& config, logger::Logger& logger)
    : config_(config), logger_(logger) {
}

bool NetworkService::is_public_ipv4(const std::string& ip) {
    if (!is_valid_ipv4(ip)) return false;
    struct in_addr addr;
    inet_pton(AF_INET, ip.c_str(), &addr);
    uint32_t ip_int = ntohl(addr.s_addr);
    return !is_private_ipv4(ip_int);
}

bool NetworkService::is_public_ipv6(const std::string& ip) {
    return is_valid_ipv6(ip) && !is_private_ipv6(ip);
}

void NetworkService::refresh() {
    refresh_requested_ = true;
}

IpDetectionResult NetworkService::public_ipv4() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cache_v4_.timestamp);

    // If refresh requested or cache expired
    if (refresh_requested_ || elapsed.count() >= kCacheTTLSeconds) {
        refresh_requested_ = false;
        if (!cache_v4_.detection_in_progress) {
            cache_v4_.detection_in_progress = true;
            lock.~lock_guard(); // release mutex before async detection

            // For simplicity in v1, run detection synchronously.
            // Background refresh can be added in a future iteration.
            auto result = detect_with_fallback(false);

            std::lock_guard<std::mutex> relock(cache_mutex_);
            cache_v4_.result = result;
            cache_v4_.timestamp = now;
            cache_v4_.detection_in_progress = false;
            save_cached("ipv4", result);
            return result;
        }
    }

    // Return cached, mark stale if older than threshold
    if (elapsed.count() >= kStaleThresholdSeconds && cache_v4_.result.success) {
        cache_v4_.result.stale = true;
    }
    return cache_v4_.result;
}

IpDetectionResult NetworkService::public_ipv6() {
    // Similar to public_ipv4 but for IPv6
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cache_v6_.timestamp);

    if (refresh_requested_ || elapsed.count() >= kCacheTTLSeconds) {
        refresh_requested_ = false;
        if (!cache_v6_.detection_in_progress) {
            cache_v6_.detection_in_progress = true;
            lock.~lock_guard();

            auto result = detect_with_fallback(true);

            std::lock_guard<std::mutex> relock(cache_mutex_);
            cache_v6_.result = result;
            cache_v6_.timestamp = now;
            cache_v6_.detection_in_progress = false;
            save_cached("ipv6", result);
            return result;
        }
    }

    if (elapsed.count() >= kStaleThresholdSeconds && cache_v6_.result.success) {
        cache_v6_.result.stale = true;
    }
    return cache_v6_.result;
}

void NetworkService::detect_now() {
    logger_.info("NETWORK", "Running initial public IP detection...");

    auto v4 = detect_with_fallback(false);
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_v4_.result = v4;
        cache_v4_.timestamp = std::chrono::steady_clock::now();
    }
    save_cached("ipv4", v4);

    auto v6 = detect_with_fallback(true);
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_v6_.result = v6;
        cache_v6_.timestamp = std::chrono::steady_clock::now();
    }
    save_cached("ipv6", v6);

    logger_.info("NETWORK", "Detection complete: v4=" + v4.address
                 + " source=" + v4.source
                 + " v6=" + v6.address
                 + " source=" + v6.source);
}

IpDetectionResult NetworkService::detect_with_fallback(bool ipv6) {
    const char* label = ipv6 ? "IPv6" : "IPv4";
    logger_.info("NETWORK", std::string("Detecting ") + label + "...");

    auto n = std::string(label);

    // Method 1: Routing table
    {
        auto t1 = std::chrono::steady_clock::now();
        auto result = detect_from_routing_table(ipv6);
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t1).count();
        if (result.success) {
            logger_.info("NETWORK", n + ": " + result.address
                         + " (source: routing_table, duration: " + std::to_string(dur) + "ms)");
            return result;
        }
        logger_.info("NETWORK", n + ": routing_table failed (" + result.error
                     + ", duration: " + std::to_string(dur) + "ms)");
    }

    // Method 2: External DNS helper via c-ares
    {
        auto t1 = std::chrono::steady_clock::now();
        auto result = detect_from_external_dns(ipv6);
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t1).count();
        if (result.success) {
            logger_.info("NETWORK", n + ": " + result.address
                         + " (source: external_dns, duration: " + std::to_string(dur) + "ms)");
            return result;
        }
        logger_.info("NETWORK", n + ": external_dns failed (" + result.error
                     + ", duration: " + std::to_string(dur) + "ms)");
    }

    // Method 3: Load cached value from Config (stale but better than nothing)
    {
        auto cached = load_cached(ipv6 ? "ipv6" : "ipv4");
        if (cached.success) {
            cached.stale = true;
            logger_.info("NETWORK", n + ": using stale cached value " + cached.address
                         + " from config (detected_at: " + cached.detected_at + ")");
            return cached;
        }
    }

    // Method 4: Server hostname DNS fallback (least preferred)
    {
        auto t1 = std::chrono::steady_clock::now();
        auto result = detect_from_hostname_dns(ipv6);
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t1).count();
        if (result.success) {
            logger_.warning("NETWORK", n + ": " + result.address
                         + " (source: hostname_dns_fallback, duration: " + std::to_string(dur)
                         + "ms) — WARNING: circular dependency risk");
            return result;
        }
        logger_.info("NETWORK", n + ": hostname_dns_fallback failed (" + result.error
                     + ", duration: " + std::to_string(dur) + "ms)");
    }

    IpDetectionResult failed;
    failed.source = "";
    failed.detected_at = timestamp_now();
    failed.error = "All detection methods failed";
    logger_.warning("NETWORK", n + ": detection failed — all methods exhausted");
    return failed;
}

IpDetectionResult NetworkService::detect_from_routing_table(bool ipv6) {
    IpDetectionResult result;
    result.detected_at = timestamp_now();

    std::string target = ipv6 ? "2600::" : "1.1.0.0";
    std::string cmd = ipv6 ? "ip" : "ip";
    std::vector<std::string> args = {cmd, "-4", "route", "get", target};
    if (ipv6) {
        args = {cmd, "-6", "route", "get", target};
    }

    auto exec_result = executor_.run(args);
    if (exec_result.exit_code != 0) {
        result.error = "ip route get failed (exit=" + std::to_string(exec_result.exit_code) + ")";
        return result;
    }

    // Parse output for "src <IP>" pattern
    // Example: "1.1.0.0 via 172.17.0.1 dev eth0 src 172.17.0.2"
    std::istringstream stream(exec_result.out);
    std::string line;
    while (std::getline(stream, line)) {
        auto src_pos = line.find(" src ");
        if (src_pos == std::string::npos) continue;
        auto ip_start = src_pos + 5;
        auto ip_end = line.find(' ', ip_start);
        if (ip_end == std::string::npos) ip_end = line.size();
        std::string ip = line.substr(ip_start, ip_end - ip_start);

        bool valid_public = false;
        if (ipv6) {
            if (is_public_ipv6(ip)) valid_public = true;
        } else {
            if (is_public_ipv4(ip)) valid_public = true;
        }

        if (valid_public) {
            result.address = ip;
            result.source = "routing_table";
            result.success = true;
            return result;
        }
    }

    result.error = "No public IP found in routing table output";
    return result;
}

IpDetectionResult NetworkService::detect_from_external_dns(bool ipv6) {
    IpDetectionResult result;
    result.detected_at = timestamp_now();

    // Initialize c-ares and query myip.opendns.com
    ares_channel_t* channel = nullptr;
    struct ares_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout = 3000;  // 3 second timeout per query
    opts.tries = 1;

    int status = ares_init_options(&channel, &opts, ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES);
    if (status != ARES_SUCCESS) {
        result.error = "Failed to init c-ares channel";
        return result;
    }

    // Point to a DNS resolver that returns the caller's IP
    // resolver1.opendns.com = 208.67.222.222
    struct ares_addr_node* servers = nullptr;
    // We need to set custom nameservers. For now, rely on system DNS.
    // myip.opendns.com resolves via system resolver and returns caller's IP.

    const char* hostname = "myip.opendns.com";
    ares_dns_rec_type_t rectype = ipv6 ? ARES_REC_TYPE_AAAA : ARES_REC_TYPE_A;

    std::string detected_ip;
    bool query_done = false;
    std::mutex mtx;

    auto callback = [](void* arg, ares_status_t astatus, size_t timeouts,
                       const ares_dns_record_t* dnsrec) noexcept {
        (void)timeouts;
        auto* ip_out = static_cast<std::string*>(arg);
        if (astatus != ARES_SUCCESS || !dnsrec) return;

        size_t cnt = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
        for (size_t i = 0; i < cnt; ++i) {
            auto* rr = ares_dns_record_rr_get_const(dnsrec, ARES_SECTION_ANSWER, i);
            if (!rr) continue;
            auto rtype = ares_dns_rr_get_type(rr);
            if (rtype == ARES_REC_TYPE_A) {
                const struct in_addr* addr = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
                if (addr) {
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, addr, buf, sizeof(buf));
                    *ip_out = buf;
                }
            } else if (rtype == ARES_REC_TYPE_AAAA) {
                const struct ares_in6_addr* a6 = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
                if (a6) {
                    char buf[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, reinterpret_cast<const struct in6_addr*>(a6), buf, sizeof(buf));
                    *ip_out = buf;
                }
            }
        }
    };

    // Wrap callback for c-areas C API
    struct CallbackWrapper {
        std::string* ip_out;
        std::mutex* mtx;
        bool* done;
    };
    CallbackWrapper wrapper{&detected_ip, &mtx, &query_done};

    auto c_callback = [](void* arg, ares_status_t status, size_t timeouts,
                          const ares_dns_record_t* dnsrec) noexcept {
        auto* w = static_cast<CallbackWrapper*>(arg);
        std::lock_guard<std::mutex> lock(*w->mtx);
        if (status == ARES_SUCCESS && dnsrec) {
            size_t cnt = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
            for (size_t i = 0; i < cnt; ++i) {
                auto* rr = ares_dns_record_rr_get_const(dnsrec, ARES_SECTION_ANSWER, i);
                if (!rr) continue;
                auto rtype = ares_dns_rr_get_type(rr);
                if (rtype == ARES_REC_TYPE_A) {
                    const struct in_addr* addr = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
                    if (addr) {
                        char buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, addr, buf, sizeof(buf));
                        *w->ip_out = buf;
                    }
                } else if (rtype == ARES_REC_TYPE_AAAA) {
                    const struct ares_in6_addr* a6 = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
                    if (a6) {
                        char buf[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, reinterpret_cast<const struct in6_addr*>(a6), buf, sizeof(buf));
                        *w->ip_out = buf;
                    }
                }
            }
        }
        *w->done = true;
    };

    ares_status_t qstatus = ares_query_dnsrec(channel, hostname, ARES_CLASS_IN,
                                                 rectype, c_callback, &wrapper, nullptr);
    if (qstatus != ARES_SUCCESS) {
        ares_destroy(channel);
        result.error = "Query failed";
        return result;
    }

    // Event loop
    while (!query_done) {
        int nfds = 0;
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        struct timeval tv, *tv_out;
        tv_out = ares_timeout(channel, nullptr, &tv);
        nfds = ares_fds(channel, &read_fds, &write_fds);
        if (nfds == 0) break;
        int sel_ret = select(nfds, &read_fds, &write_fds, nullptr, tv_out);
        if (sel_ret < 0) break;
        ares_process(channel, &read_fds, &write_fds);
    }

    ares_destroy(channel);

    if (detected_ip.empty()) {
        result.error = "No IP returned by external DNS";
        return result;
    }

    result.address = detected_ip;
    result.source = "external_dns";
    result.success = true;
    return result;
}

IpDetectionResult NetworkService::detect_from_hostname_dns(bool ipv6) {
    IpDetectionResult result;
    result.detected_at = timestamp_now();

    std::string hostname = config_.server_hostname();
    if (hostname.empty()) {
        result.error = "server_hostname not configured";
        return result;
    }

    // Use c-ares to resolve hostname
    ares_channel_t* channel = nullptr;
    struct ares_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout = 3000;
    opts.tries = 1;

    int status = ares_init_options(&channel, &opts, ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES);
    if (status != ARES_SUCCESS) {
        result.error = "Failed to init c-ares channel";
        return result;
    }

    ares_dns_rec_type_t rectype = ipv6 ? ARES_REC_TYPE_AAAA : ARES_REC_TYPE_A;
    std::string detected_ip;
    bool query_done = false;
    std::mutex mtx;

    struct CbWrap {
        std::string* ip;
        std::mutex* m;
        bool* done;
    };
    CbWrap wrap{&detected_ip, &mtx, &query_done};

    auto c_callback = [](void* arg, ares_status_t astatus, size_t,
                          const ares_dns_record_t* dnsrec) noexcept {
        auto* w = static_cast<CbWrap*>(arg);
        std::lock_guard<std::mutex> lock(*w->m);
        if (astatus != ARES_SUCCESS || !dnsrec) { *w->done = true; return; }
        size_t cnt = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
        for (size_t i = 0; i < cnt; ++i) {
            auto* rr = ares_dns_record_rr_get_const(dnsrec, ARES_SECTION_ANSWER, i);
            if (!rr) continue;
            auto rt = ares_dns_rr_get_type(rr);
            if (rt == ARES_REC_TYPE_A) {
                auto* a = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
                if (a) { char b[INET_ADDRSTRLEN]; inet_ntop(AF_INET, a, b, sizeof(b)); *w->ip = b; }
            } else if (rt == ARES_REC_TYPE_AAAA) {
                auto* a6 = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
                if (a6) { char b[INET6_ADDRSTRLEN]; inet_ntop(AF_INET6, (const in6_addr*)a6, b, sizeof(b)); *w->ip = b; }
            }
        }
        *w->done = true;
    };

    ares_query_dnsrec(channel, hostname.c_str(), ARES_CLASS_IN, rectype, c_callback, &wrap, nullptr);

    while (!query_done) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds); FD_ZERO(&wfds);
        timeval tv, *tvp = ares_timeout(channel, nullptr, &tv);
        int n = ares_fds(channel, &rfds, &wfds);
        if (n == 0) break;
        select(n, &rfds, &wfds, nullptr, tvp);
        ares_process(channel, &rfds, &wfds);
    }

    ares_destroy(channel);

    if (detected_ip.empty()) {
        result.error = "No A/AAAA record for server_hostname";
        return result;
    }

    result.address = detected_ip;
    result.source = "hostname_dns_fallback";
    result.success = true;
    return result;
}

void NetworkService::save_cached(const std::string& key, const IpDetectionResult& result) {
    std::string path = config_.data_root() + "/data/public_" + key;
    std::ofstream f(path);
    if (f.is_open()) {
        f << result.address << "\n"
          << result.source << "\n"
          << result.detected_at << "\n"
          << (result.success ? "1" : "0") << "\n";
    }
}

IpDetectionResult NetworkService::load_cached(const std::string& key) {
    IpDetectionResult result;
    std::string path = config_.data_root() + "/data/public_" + key;
    std::ifstream f(path);
    if (f.is_open()) {
        std::getline(f, result.address);
        std::getline(f, result.source);
        std::getline(f, result.detected_at);
        std::string success_str;
        std::getline(f, success_str);
        result.success = (success_str == "1");
        result.stale = true;  // loaded from disk is always potentially stale
    }
    return result;
}

} // namespace containercp::network
