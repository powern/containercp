#include "MailDomainManager.h"
#include "utils/Validator.h"

#include <chrono>
#include <ctime>

namespace containercp::mail {

static std::string now_utc() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&tt, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

std::string MailDomainManager::validate_mode_relay(
    MailDomainMode mode, const std::string& relay_host) {
    if ((mode == MailDomainMode::ExternalRelay ||
         mode == MailDomainMode::SplitM365) && relay_host.empty()) {
        return "relay_host is required for " +
               mail_domain_mode_to_string(mode) + " mode";
    }
    return "";
}

void MailDomainManager::set_smarthost(const SmarthostConfig& cfg) {
    smarthost_ = cfg;
}

std::string MailDomainManager::smarthost_to_string() const {
    return std::string(smarthost_.enabled ? "1" : "0") + "|"
        + smarthost_.host + "|"
        + std::to_string(smarthost_.port) + "|"
        + smarthost_.username + "|"
        + smarthost_.password;
}

void MailDomainManager::smarthost_from_string(const std::string& s) {
    if (s.empty()) return;
    std::istringstream ss(s);
    std::string token;
    if (std::getline(ss, token, '|')) smarthost_.enabled = (token == "1");
    if (std::getline(ss, token, '|')) smarthost_.host = token;
    if (std::getline(ss, token, '|')) smarthost_.port = token.empty() ? 587 : std::stoi(token);
    if (std::getline(ss, token, '|')) smarthost_.username = token;
    if (std::getline(ss, token, '|')) smarthost_.password = token;
}

uint64_t MailDomainManager::create(const std::string& domain_name,
                                    MailDomainMode mode,
                                    uint64_t domain_id,
                                    uint64_t site_id,
                                    const std::string& relay_host) {
    // Validate mode+relay before creating
    std::string vr = validate_mode_relay(mode, relay_host);
    if (!vr.empty()) return 0;

    for (const auto& existing : domains_) {
        if (existing.domain_name == domain_name) {
            return 0;
        }
    }
    MailDomain m;
    m.id = next_id_++;
    m.name = domain_name;
    m.domain_name = domain_name;
    m.domain_id = domain_id;
    m.site_id = site_id;
    m.mode = mode;
    m.relay_host = relay_host;
    m.enabled = true;
    m.created_at = now_utc();
    m.updated_at = m.created_at;
    domains_.push_back(std::move(m));
    return m.id;
}

bool MailDomainManager::remove(uint64_t id) {
    for (auto it = domains_.begin(); it != domains_.end(); ++it) {
        if (it->id == id) {
            domains_.erase(it);
            return true;
        }
    }
    return false;
}

MailDomain* MailDomainManager::find(uint64_t id) {
    for (auto& m : domains_) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

MailDomain* MailDomainManager::find_by_domain(const std::string& domain_name) {
    for (auto& m : domains_) {
        if (m.domain_name == domain_name) {
            return &m;
        }
    }
    return nullptr;
}

const std::vector<MailDomain>& MailDomainManager::list() const {
    return domains_;
}

void MailDomainManager::set_domains(const std::vector<MailDomain>& domains) {
    domains_ = domains;
    next_id_ = 1;
    for (const auto& m : domains_) {
        if (m.id >= next_id_) {
            next_id_ = m.id + 1;
        }
    }
}

} // namespace containercp::mail
