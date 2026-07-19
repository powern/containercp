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

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
int ares_fds_compat(ares_channel_t* channel, fd_set* read_fds, fd_set* write_fds) {
    return ares_fds(channel, read_fds, write_fds);
}

void ares_process_compat(ares_channel_t* channel, fd_set* read_fds, fd_set* write_fds) {
    ares_process(channel, read_fds, write_fds);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct QueryState {
    std::string type_name;
    PerTypeResult* result_out;
    std::mutex* mutex;
    bool* done;
};

void dns_callback(void* arg, ares_status_t status, size_t timeouts,
                   const ares_dns_record_t* dnsrec) noexcept {
    (void)timeouts;
    auto* state = static_cast<QueryState*>(arg);
    std::lock_guard<std::mutex> lock(*state->mutex);

    if (status != ARES_SUCCESS) {
        if (status == ARES_ENODATA) {
            state->result_out->status_code = "NODATA";
        } else if (status == ARES_ENOTFOUND) {
            state->result_out->status_code = "NXDOMAIN";
            state->result_out->error = "NXDOMAIN";
        } else if (status == ARES_ESERVFAIL) {
            state->result_out->status_code = "SERVFAIL";
            state->result_out->error = "SERVFAIL";
        } else if (status == ARES_ETIMEOUT) {
            state->result_out->status_code = "TIMEOUT";
            state->result_out->error = "TIMEOUT";
        } else {
            state->result_out->status_code = "ERROR";
            state->result_out->error = "DNS_ERROR";
        }
        *state->done = true;
        return;
    }

    if (!dnsrec) {
        state->result_out->status_code = "ERROR";
        state->result_out->error = "Empty response";
        *state->done = true;
        return;
    }

    state->result_out->status_code = "NOERROR";
    std::vector<DnsRecord> records;

    size_t rr_count = ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER);
    for (size_t i = 0; i < rr_count; ++i) {
        const ares_dns_rr_t* rr = ares_dns_record_rr_get_const(
            dnsrec, ARES_SECTION_ANSWER, i);
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
                const struct ares_in6_addr* ares_addr =
                    ares_dns_rr_get_addr6(rr, ARES_RR_AAAA_ADDR);
                if (ares_addr) {
                    const struct in6_addr* addr =
                        reinterpret_cast<const struct in6_addr*>(ares_addr);
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
                const char* mname_ref = ares_dns_rr_get_str(rr, ARES_RR_SOA_MNAME);
                const char* rname_ref = ares_dns_rr_get_str(rr, ARES_RR_SOA_RNAME);
                uint32_t serial = ares_dns_rr_get_u32(rr, ARES_RR_SOA_SERIAL);
                rec.value = (mname_ref ? mname_ref : "") + std::string(" ")
                          + (rname_ref ? rname_ref : "");
                detail << "SOA " << (mname_ref ? mname_ref : "") << " "
                       << (rname_ref ? rname_ref : "")
                       << " serial=" << serial << " (ttl=" << rec.ttl << ")";
                break;
            }
            case ARES_REC_TYPE_CAA: {
                uint8_t critical = ares_dns_rr_get_u8(rr, ARES_RR_CAA_CRITICAL);
                const char* tag = ares_dns_rr_get_str(rr, ARES_RR_CAA_TAG);
                size_t val_len = 0;
                const unsigned char* caa_val =
                    ares_dns_rr_get_bin(rr, ARES_RR_CAA_VALUE, &val_len);
                rec.value = (critical ? "1" : "0") + std::string(" ");
                rec.value += tag ? tag : "";
                rec.value += " \"";
                rec.value += caa_val
                    ? std::string(reinterpret_cast<const char*>(caa_val), val_len) : "";
                rec.value += "\"";
                detail << "CAA " << (critical ? "critical" : "non-critical")
                       << " " << (tag ? tag : "") << " \""
                       << (caa_val
                              ? std::string(reinterpret_cast<const char*>(caa_val), val_len)
                              : "")
                       << "\" (ttl=" << rec.ttl << ")";
                break;
            }
            default:
                continue;
        }

        rec.dns_response_details = detail.str();
        records.push_back(std::move(rec));
    }

    state->result_out->records = std::move(records);
    *state->done = true;
}

// Extract SOA fields from the SOA per-type result
void extract_soa_fields(const std::vector<DnsRecord>& soa_records,
                        std::string& mname, std::string& rname, uint64_t& serial) {
    for (const auto& rec : soa_records) {
        if (rec.type == "SOA" && !rec.value.empty()) {
            // SOA value format: "mname rname" — extract from dns_response_details
            // which was built as: "SOA <mname> <rname> serial=<N> (ttl=<N>)"
            const auto& d = rec.dns_response_details;
            auto soa_pos = d.find("SOA ");
            if (soa_pos == std::string::npos) continue;
            auto rest = d.substr(soa_pos + 4);
            auto space1 = rest.find(' ');
            if (space1 == std::string::npos) continue;
            auto space2 = rest.find(' ', space1 + 1);
            if (space2 == std::string::npos) continue;
            mname = rest.substr(0, space1);
            rname = rest.substr(space1 + 1, space2 - space1 - 1);
            auto serial_pos = rest.find("serial=");
            if (serial_pos != std::string::npos) {
                auto num_start = serial_pos + 7;
                auto num_end = rest.find(' ', num_start);
                try {
                    serial = std::stoull(rest.substr(num_start, num_end - num_start));
                } catch (...) {}
            }
            return;
        }
    }
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
    if (domain.front() == '.' || domain.back() == '.') return false;

    // Check overall characters
    bool has_alpha = false;
    for (char c : domain) {
        if (std::isalpha(static_cast<unsigned char>(c))) has_alpha = true;
        else if (c == '-') continue;
        else if (c == '.') continue;
        else if (std::isdigit(static_cast<unsigned char>(c))) continue;
        else return false;
    }
    if (!has_alpha) return false;  // reject pure IPs

    // Check each label
    size_t start = 0;
    while (start < domain.size()) {
        auto end = domain.find('.', start);
        if (end == std::string::npos) end = domain.size();
        size_t label_len = end - start;
        if (label_len == 0) return false;      // empty label (bad..example)
        if (label_len > 63) return false;       // label too long
        if (domain[start] == '-') return false;  // label starts with hyphen
        if (domain[end - 1] == '-') return false; // label ends with hyphen
        start = end + 1;
    }

    return true;
}

bool DnsCheckService::validate_dns_name(const std::string& name) {
    // More permissive than validate_domain(): allows underscore for DNS service
    // records like _dmarc, _domainkey, _mta-sts, _smtp._tls, etc.
    // Still protects against shell injection, spaces, and invalid DNS names.
    if (name.empty() || name.size() > 253) return false;
    if (name.front() == '.' || name.back() == '.') return false;

    bool has_alpha = false;

    // Check for shell/control characters, spaces, and path separators
    for (char c : name) {
        if (c <= 32 || c >= 127) return false;  // non-printable or DEL
        if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '%'
            || c == ';' || c == '|' || c == '`' || c == '~' || c == '&'
            || c == '{' || c == '}' || c == '(' || c == ')' || c == '$'
            || c == '!' || c == '@' || c == '^' || c == '*' || c == '+'
            || c == '=' || c == '[' || c == ']' || c == ':' || c == '"'
            || c == '\'') return false;
        // Allow: alphanumeric, dots, hyphens, underscores
        if (std::isalpha(static_cast<unsigned char>(c))) has_alpha = true;
        else if (std::isdigit(static_cast<unsigned char>(c))) continue;
        else if (c == '.') continue;
        else if (c == '-') continue;
        else if (c == '_') continue;
        else return false;
    }

    // Reject pure numeric (IP addresses)
    if (!has_alpha) return false;

    // Check each label
    size_t start = 0;
    while (start < name.size()) {
        auto end = name.find('.', start);
        if (end == std::string::npos) end = name.size();
        size_t label_len = end - start;
        if (label_len == 0) return false;   // empty label (bad..example)
        if (label_len > 63) return false;   // label too long
        // Reject leading/trailing hyphen in labels
        if (name[start] == '-') return false;
        if (name[end - 1] == '-') return false;
        start = end + 1;
    }

    return true;
}

bool DnsCheckService::validate_type(const std::string& type) {
    return ares_type_from_string(type) != ARES_REC_TYPE_ANY;
}

void DnsCheckService::set_cache_ttl(int seconds) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_ttl_ = seconds;
}

void DnsCheckService::clear_cache(const std::string& domain) {
    // Normalize to lowercase for consistent cache key matching
    std::string normalized;
    normalized.reserve(domain.size());
    for (char c : domain) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto prefix = normalized + ":";
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.substr(0, prefix.size()) == prefix) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

bool DnsCheckService::has_cached(const std::string& domain) const {
    std::string normalized;
    normalized.reserve(domain.size());
    for (char c : domain) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto prefix = normalized + ":";
    time_t now = std::time(nullptr);
    for (const auto& [key, entry] : cache_) {
        if (key.substr(0, prefix.size()) == prefix) {
            return (now - entry.timestamp) < cache_ttl_;
        }
    }
    return false;
}

DnsCheckResult DnsCheckService::check(const std::string& domain,
                                        const std::vector<std::string>& record_types) {
    // Normalize to lowercase
    std::string normalized;
    normalized.reserve(domain.size());
    for (char c : domain) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // Use validate_dns_name() — allows underscore for service records
    // (_dmarc, _domainkey, _mta-sts, etc.) while still rejecting
    // shell chars, spaces, path separators, and invalid DNS labels.
    if (!validate_dns_name(normalized)) {
        DnsCheckResult r;
        r.success = false;
        r.error = "Invalid domain format";
        r.overall_status = "failed";
        return r;
    }

    for (const auto& t : record_types) {
        if (!validate_type(t)) {
            DnsCheckResult r;
            r.success = false;
            r.error = "Unsupported DNS record type: " + t;
            r.overall_status = "failed";
            return r;
        }
    }

    // Build cache key
    std::vector<std::string> sorted_types = record_types;
    std::sort(sorted_types.begin(), sorted_types.end());
    std::string cache_key = normalized + ":";
    for (const auto& t : sorted_types) cache_key += t + ",";

    // Check cache (thread-safe)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(cache_key);
        if (it != cache_.end() && (std::time(nullptr) - it->second.timestamp) < cache_ttl_) {
            DnsCheckResult cached = it->second.result;
            cached.cached = true;
            return cached;
        }
    }

    DnsCheckResult result = do_check(normalized, record_types);

    // Store in cache (thread-safe)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        CacheEntry entry{result, std::time(nullptr)};
        cache_[cache_key] = entry;
    }

    return result;
}

DnsCheckResult DnsCheckService::do_check(const std::string& domain,
                                           const std::vector<std::string>& record_types) {
    DnsCheckResult result;
    result.domain = domain;

    // Thread-safe timestamp
    std::time_t now = std::time(nullptr);
    struct tm utc_tm;
    gmtime_r(&now, &utc_tm);
    char ts_buf[64];
    if (std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &utc_tm)) {
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
        result.overall_status = "failed";
        return result;
    }

    std::mutex result_mutex;
    result.per_type.resize(record_types.size());

    for (size_t i = 0; i < record_types.size(); ++i) {
        const auto& type = record_types[i];
        ares_dns_rec_type_t ares_type = ares_type_from_string(type);
        if (ares_type == ARES_REC_TYPE_ANY) continue;

        result.per_type[i].type = type;
        bool done = false;
        QueryState query_state{type, &result.per_type[i], &result_mutex, &done};

        ares_status_t qstatus = ares_query_dnsrec(channel, domain.c_str(), ARES_CLASS_IN,
                                                     ares_type, dns_callback, &query_state, nullptr);
        if (qstatus != ARES_SUCCESS) {
            result.per_type[i].status_code = "ERROR";
            result.per_type[i].error = "DNS_QUERY_FAILED";
            done = true;
        }

        // Process until this query completes
        while (!done) {
            int nfds = 0;
            fd_set read_fds, write_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            struct timeval tv, *tv_out;

            tv_out = ares_timeout(channel, nullptr, &tv);
            nfds = ares_fds_compat(channel, &read_fds, &write_fds);
            if (nfds == 0) break;

            int sel_ret = select(nfds, &read_fds, &write_fds, nullptr, tv_out);
            if (sel_ret < 0) break;

            ares_process_compat(channel, &read_fds, &write_fds);
        }
    }

    ares_destroy(channel);

    // Extract SOA fields from per_type results
    for (const auto& pt : result.per_type) {
        if (pt.type == "SOA") {
            extract_soa_fields(pt.records, result.soa.mname,
                               result.soa.rname, result.soa.serial);
        }
    }

    result.overall_status = compute_overall_status(result.per_type, result.success, result.error);
    return result;
}

std::string DnsCheckService::compute_overall_status(
    const std::vector<PerTypeResult>& per_type,
    bool& success_out, std::string& error_out) {
    // NXDOMAIN and NODATA are valid DNS responses — not failures
    size_t ok_count = 0, nodata_count = 0, nxdomain_count = 0, fail_count = 0;
    std::string first_error;

    for (const auto& pt : per_type) {
        if (pt.status_code == "NOERROR") {
            ok_count++;
        } else if (pt.status_code == "NODATA") {
            nodata_count++;
        } else if (pt.status_code == "NXDOMAIN") {
            nxdomain_count++;
            // NXDOMAIN is a valid DNS diagnostic result — not a failure
        } else {
            fail_count++;
            if (first_error.empty() && !pt.error.empty()) {
                first_error = pt.error;
            }
        }
    }

    size_t valid_count = ok_count + nodata_count + nxdomain_count;

    if (fail_count == 0) {
        success_out = true;
        error_out.clear();
        return "complete";
    } else if (valid_count > 0) {
        success_out = true;
        error_out = first_error.empty() ? std::string("Some queries failed") : first_error;
        return "partial";
    } else {
        success_out = false;
        error_out = first_error.empty() ? std::string("All queries failed") : first_error;
        return "failed";
    }
}

int DnsCheckService::compute_http_status(
    const std::vector<PerTypeResult>& per_type,
    bool overall_success) {
    if (!overall_success) return 502;

    // Check if ANY type has a resolver failure
    bool has_resolver_failure = false;
    for (const auto& pt : per_type) {
        if (pt.status_code == "SERVFAIL" || pt.status_code == "TIMEOUT"
            || pt.status_code == "ERROR") {
            has_resolver_failure = true;
            break;
        }
    }

    // 502 only if ALL results are resolver failures (already handled by success=false)
    if (has_resolver_failure) {
        // But if overall_success is true (partial), it means SOME types succeeded
        // so we return 200
        return 200;
    }

    return 200;
}

} // namespace containercp::dns
