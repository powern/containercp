#include "DnsCheckService.h"

#include <algorithm>
#include <arpa/inet.h>
#include <ares.h>
#include <ares_dns_record.h>
#include <cctype>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <vector>

namespace containercp::dns {

namespace {

struct TypeMapping {
    const char* name;
    ares_dns_rec_type_t ares_type;
};

const TypeMapping kSupportedTypes[] = {
    {"A",     ARES_REC_TYPE_A},
    {"AAAA",  ARES_REC_TYPE_AAAA},
    {"MX",    ARES_REC_TYPE_MX},
    {"TXT",   ARES_REC_TYPE_TXT},
    {"CNAME", ARES_REC_TYPE_CNAME},
    {"NS",    ARES_REC_TYPE_NS},
    {"SOA",   ARES_REC_TYPE_SOA},
    {"CAA",   ARES_REC_TYPE_CAA},
};

constexpr int kNumTypes = sizeof(kSupportedTypes) / sizeof(kSupportedTypes[0]);

ares_dns_rec_type_t ares_type_from_string(const std::string& type) {
    for (int i = 0; i < kNumTypes; ++i) {
        if (type == kSupportedTypes[i].name) {
            return kSupportedTypes[i].ares_type;
        }
    }
    return ARES_REC_TYPE_ANY;
}

struct QueryState {
    std::string type_name;
    std::vector<DnsRecord>* records;
    std::mutex* mutex;
    bool* done;
    std::string* error_out;
};

void dns_callback(void* arg, ares_status_t status, size_t timeouts,
                   const ares_dns_record_t* dnsrec) noexcept {
    (void)timeouts;
    auto* state = static_cast<QueryState*>(arg);
    std::lock_guard<std::mutex> lock(*state->mutex);

    if (status != ARES_SUCCESS) {
        if (status == ARES_ENODATA) {
            // NODATA: domain exists, no records of this type — not an error
        } else if (status == ARES_ENOTFOUND) {
            if (state->error_out->empty())
                *state->error_out = "NXDOMAIN";
        } else if (status == ARES_ESERVFAIL) {
            if (state->error_out->empty())
                *state->error_out = "SERVFAIL";
        } else if (status == ARES_ETIMEOUT) {
            if (state->error_out->empty())
                *state->error_out = "TIMEOUT";
        } else {
            if (state->error_out->empty())
                *state->error_out = "DNS_ERROR";
        }
        *state->done = true;
        return;
    }

    if (!dnsrec) {
        *state->done = true;
        return;
    }

    size_t rr_count = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
    for (size_t i = 0; i < rr_count; ++i) {
        const ares_dns_rr_t* rr = ares_dns_record_rr_get_const(dnsrec, ARES_SECTION_ANSWER, i);
        if (!rr) continue;

        DnsRecord rec;
        rec.type = state->type_name;

        const char* name = ares_dns_rr_get_name(rr);
        rec.name = name ? name : "";
        rec.ttl = static_cast<int>(ares_dns_rr_get_ttl(rr));

        std::ostringstream detail;

        ares_dns_rec_type_t rtype = ares_dns_rr_get_type(rr);
        switch (rtype) {
            case ARES_REC_TYPE_A: {
                const struct in_addr* addr = ares_dns_rr_get_addr(rr, ARES_RR_A_ADDR);
                if (addr) {
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, addr, buf, sizeof(buf));
                    rec.value = buf;
                    detail << "A " << buf << " (ttl=" << rec.ttl << ")";
                }
                break;
            }
            case ARES_REC_TYPE_AAAA: {
                const struct ares_in6_addr* ares_addr = ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
                if (ares_addr) {
                    const struct in6_addr* addr = reinterpret_cast<const struct in6_addr*>(ares_addr);
                    char buf[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, addr, buf, sizeof(buf));
                    rec.value = buf;
                    detail << "AAAA " << buf << " (ttl=" << rec.ttl << ")";
                }
                break;
            }
            case ARES_REC_TYPE_MX: {
                uint16_t priority = ares_dns_rr_get_u16(rr, ARES_RR_MX_PREFERENCE);
                const char* exchange = ares_dns_rr_get_str(rr, ARES_RR_MX_EXCHANGE);
                rec.priority = priority;
                rec.value = exchange ? exchange : "";
                detail << "MX " << priority << " " << (exchange ? exchange : "")
                       << " (ttl=" << rec.ttl << ")";
                break;
            }
            case ARES_REC_TYPE_TXT: {
                size_t txt_len = 0;
                const unsigned char* txt = ares_dns_rr_get_bin(rr, ARES_RR_TXT_DATA, &txt_len);
                if (txt) {
                    rec.value = std::string(reinterpret_cast<const char*>(txt), txt_len);
                    detail << "TXT \"" << rec.value << "\" (ttl=" << rec.ttl << ")";
                }
                break;
            }
            case ARES_REC_TYPE_CNAME: {
                const char* cname = ares_dns_rr_get_str(rr, ARES_RR_CNAME_CNAME);
                rec.value = cname ? cname : "";
                detail << "CNAME " << rec.value << " (ttl=" << rec.ttl << ")";
                break;
            }
            case ARES_REC_TYPE_NS: {
                const char* ns = ares_dns_rr_get_str(rr, ARES_RR_NS_NSDNAME);
                rec.value = ns ? ns : "";
                detail << "NS " << rec.value << " (ttl=" << rec.ttl << ")";
                break;
            }
            case ARES_REC_TYPE_SOA: {
                const char* mname = ares_dns_rr_get_str(rr, ARES_RR_SOA_MNAME);
                const char* rname = ares_dns_rr_get_str(rr, ARES_RR_SOA_RNAME);
                uint32_t serial = ares_dns_rr_get_u32(rr, ARES_RR_SOA_SERIAL);
                rec.value = (mname ? mname : "") + std::string(" ") + (rname ? rname : "");
                detail << "SOA " << (mname ? mname : "") << " " << (rname ? rname : "")
                       << " serial=" << serial << " (ttl=" << rec.ttl << ")";
                break;
            }
            case ARES_REC_TYPE_CAA: {
                uint8_t critical = ares_dns_rr_get_u8(rr, ARES_RR_CAA_CRITICAL);
                const char* tag = ares_dns_rr_get_str(rr, ARES_RR_CAA_TAG);
                size_t val_len = 0;
                const unsigned char* caa_val = ares_dns_rr_get_bin(rr, ARES_RR_CAA_VALUE, &val_len);
                rec.value = (critical ? "1" : "0") + std::string(" ");
                rec.value += tag ? tag : "";
                rec.value += " \"";
                rec.value += caa_val ? std::string(reinterpret_cast<const char*>(caa_val), val_len) : "";
                rec.value += "\"";
                detail << "CAA " << (critical ? "critical" : "non-critical")
                       << " " << (tag ? tag : "")
                       << " \"" << (caa_val ? std::string(reinterpret_cast<const char*>(caa_val), val_len) : "") << "\""
                       << " (ttl=" << rec.ttl << ")";
                break;
            }
            default:
                continue;
        }

        rec.dns_response_details = detail.str();
        state->records->push_back(std::move(rec));
    }

    *state->done = true;
}

struct AresGlobalInit {
    AresGlobalInit() { ares_library_init(ARES_LIB_INIT_ALL); }
    ~AresGlobalInit() { ares_library_cleanup(); }
};

} // anonymous namespace

DnsCheckService::DnsCheckService() {
    static AresGlobalInit ares_init;
}

bool DnsCheckService::validate_domain(const std::string& domain) {
    if (domain.empty() || domain.size() > 253) return false;
    // Reject IP addresses (all digits and dots)
    bool has_alpha = false;
    bool has_hyphen = false;
    for (char c : domain) {
        if (std::isalpha(static_cast<unsigned char>(c))) has_alpha = true;
        else if (c == '-') has_hyphen = true;
        else if (c == '.') continue;
        else if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    if (domain.front() == '.' || domain.back() == '.') return false;
    // Must have at least one alphabetic character (reject pure IPs)
    if (!has_alpha) return false;
    return true;
}

bool DnsCheckService::validate_type(const std::string& type) {
    return ares_type_from_string(type) != ARES_REC_TYPE_ANY;
}

void DnsCheckService::set_cache_ttl(int seconds) {
    cache_ttl_ = seconds;
}

void DnsCheckService::clear_cache(const std::string& domain) {
    auto prefix = domain + ":";
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.substr(0, prefix.size()) == prefix) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

bool DnsCheckService::has_cached(const std::string& domain) const {
    auto prefix = domain + ":";
    for (const auto& [key, entry] : cache_) {
        if (key.substr(0, prefix.size()) == prefix) {
            return (std::time(nullptr) - entry.timestamp) < cache_ttl_;
        }
    }
    return false;
}

DnsCheckResult DnsCheckService::check(const std::string& domain,
                                        const std::vector<std::string>& record_types) {
    if (!validate_domain(domain)) {
        DnsCheckResult r;
        r.success = false;
        r.error = "Invalid domain format";
        r.status_code = "INVALID";
        return r;
    }

    for (const auto& t : record_types) {
        if (!validate_type(t)) {
            DnsCheckResult r;
            r.success = false;
            r.error = "Unsupported DNS record type: " + t;
            r.status_code = "INVALID";
            return r;
        }
    }

    std::vector<std::string> sorted_types = record_types;
    std::sort(sorted_types.begin(), sorted_types.end());
    std::string cache_key = domain + ":";
    for (const auto& t : sorted_types) cache_key += t + ",";

    auto it = cache_.find(cache_key);
    if (it != cache_.end() && (std::time(nullptr) - it->second.timestamp) < cache_ttl_) {
        return it->second.result;
    }

    DnsCheckResult result = do_check(domain, record_types);

    CacheEntry entry{result, std::time(nullptr)};
    cache_[cache_key] = entry;

    return result;
}

DnsCheckResult DnsCheckService::do_check(const std::string& domain,
                                           const std::vector<std::string>& record_types) {
    DnsCheckResult result;
    result.domain = domain;

    std::time_t now = std::time(nullptr);
    char ts_buf[64];
    if (std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now))) {
        result.resolved_at = ts_buf;
    }

    ares_channel_t* channel = nullptr;
    struct ares_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.timeout = 5000;
    opts.tries = 2;
    int status = ares_init_options(&channel, &opts, ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES);
    if (status != ARES_SUCCESS) {
        result.success = false;
        result.error = "Failed to initialise DNS channel";
        result.status_code = "INTERNAL";
        return result;
    }

    std::vector<DnsRecord> records;
    std::string error_str;
    std::mutex mtx;

    for (const auto& type : record_types) {
        ares_dns_rec_type_t ares_type = ares_type_from_string(type);
        if (ares_type == ARES_REC_TYPE_ANY) continue;

        bool done = false;
        QueryState query_state{type, &records, &mtx, &done, &error_str};

        ares_status_t qstatus = ares_query_dnsrec(channel, domain.c_str(), ARES_CLASS_IN,
                                                     ares_type, dns_callback, &query_state, nullptr);
        if (qstatus != ARES_SUCCESS) {
            if (error_str.empty()) {
                error_str = "DNS_QUERY_FAILED";
            }
            done = true;
        }

        while (!done) {
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
    }

    ares_destroy(channel);

    result.records = std::move(records);

    if (!error_str.empty()) {
        result.success = false;
        result.error = error_str;
        result.status_code = error_str;
    } else {
        result.success = true;
        result.status_code = "NOERROR";
    }

    return result;
}

} // namespace containercp::dns
