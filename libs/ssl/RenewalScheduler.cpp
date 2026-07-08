#include "RenewalScheduler.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace containercp::ssl {

RenewalScheduler::RenewalScheduler(
    logger::Logger& logger,
    CertificateStore& store,
    jobs::JobManager& job_manager,
    jobs::JobExecutor& executor,
    std::unordered_map<std::string, std::shared_ptr<CertificateProvider>>& providers)
    : logger_(logger)
    , store_(store)
    , job_manager_(job_manager)
    , executor_(executor)
    , providers_(providers)
{
}

RenewalScheduler::~RenewalScheduler() {
    shutdown();
}

void RenewalScheduler::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&RenewalScheduler::scheduler_loop, this);
    logger_.info("Renewal", "Scheduler started (interval: " + std::to_string(interval_.count() / 3600) + "h)");
}

void RenewalScheduler::shutdown() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) {
        thread_.join();
    }
    logger_.info("Renewal", "Scheduler stopped");
}

void RenewalScheduler::set_interval_hours(int hours) {
    interval_ = std::chrono::seconds(std::max(hours, 1) * 3600);
}

std::string RenewalScheduler::next_check_at() const {
    return next_check_at_;
}

void RenewalScheduler::check_now() {
    logger_.info("Renewal", "Running manual renewal check");

    // Find all certificates with auto_renew enabled
    auto site_ids = store_.enumerate();
    int checked = 0;
    int renewed = 0;
    int skipped = 0;
    int failed = 0;

    for (auto site_id : site_ids) {
        auto load_result = store_.load_metadata(site_id);
        if (!load_result.success) {
            skipped++;
            continue;
        }

        auto& meta = load_result.metadata;

        // Skip sites without auto-renew
        if (!meta.auto_renew) {
            logger_.info("Renewal", "Skipped site " + std::to_string(site_id) + ": auto_renew disabled");
            skipped++;
            continue;
        }

        // Skip non-active certificates
        if (meta.status != "active") {
            skipped++;
            continue;
        }

        // Check if provider supports auto-renew
        auto prov_it = providers_.find(meta.provider_id);
        if (prov_it == providers_.end() || !prov_it->second->supports_auto_renew()) {
            logger_.info("Renewal", "Skipped site " + std::to_string(site_id)
                         + ": provider does not support auto-renew");
            skipped++;
            continue;
        }

        // Determine domain name for logging
        std::string domain = meta.domains.empty() ? std::to_string(site_id) : meta.domains[0];

        // Skip if expires_at is not set (newly issued, no expiry known)
        if (meta.expires_at.empty()) {
            logger_.info("Renewal", domain + ": expires_at not set, skipping renewal");
            skipped++;
            continue;
        }

        // Calculate days until expiry
        int days_until_expiry = 999;
        {
            struct tm tm = {};
            int y, M, d, h, m, s;
            if (sscanf(meta.expires_at.c_str(), "%d-%d-%dT%d:%d:%dZ", &y, &M, &d, &h, &m, &s) == 6) {
                tm.tm_year = y - 1900;
                tm.tm_mon = M - 1;
                tm.tm_mday = d;
                tm.tm_hour = h;
                tm.tm_min = m;
                tm.tm_sec = s;
                time_t exp_time = timegm(&tm);
                time_t now = time(nullptr);
                double diff = difftime(exp_time, now);
                days_until_expiry = (int)(diff / 86400.0);
            }
        }

        const int renewal_window_days = 30;
        logger_.info("Renewal", domain + ": expires_at=" + meta.expires_at
                     + " days_until_expiry=" + std::to_string(days_until_expiry)
                     + " window=" + std::to_string(renewal_window_days) + "d");

        // Only renew if within renewal window
        if (days_until_expiry > renewal_window_days) {
            logger_.info("Renewal", domain + ": not due yet, skipping");
            skipped++;
            continue;
        }

        // Check if we're in a backoff period
        if (!meta.last_validation.empty()) {
            std::string next_attempt = meta.last_validation;
            // Simple comparison: if next_attempt is in the future, skip
            if (next_attempt > CertificateStore::timestamp_utc()) {
                logger_.info("Renewal", "Skipped site " + std::to_string(site_id)
                             + ": in backoff until " + next_attempt);
                skipped++;
                continue;
            }
        }

        checked++;

        // Enqueue renewal via JobExecutor
        std::string provider_id = meta.provider_id;

        std::vector<std::string> steps = {"Checking certificate...", "Renewing...", "Finalizing..."};
        uint64_t job_id = job_manager_.create("ssl-renew", steps);
        job_manager_.update(job_id, "pending", 0, "Queued by scheduler");

        bool submitted = executor_.submit(job_id,
            [this, site_id, domain, provider_id, job_id](jobs::JobManager& jm, uint64_t jid) {
                jm.update(jid, "running", 10, "Renewing certificate...");

                auto prov_it = providers_.find(provider_id);
                if (prov_it == providers_.end()) {
                    jm.update(jid, "failed", 100, "Provider not found: " + provider_id);
                    // Backoff: update metadata with next_attempt
                    auto meta_result = store_.load_metadata(site_id);
                    if (meta_result.success) {
                        auto m = meta_result.metadata;
                        m.renew_attempts++;
                        m.last_error = "Provider not found";
                        store_.save_metadata(site_id, m);
                    }
                    return;
                }

                auto result = prov_it->second->renew(domain);
                if (result.success) {
                    jm.update(jid, "completed", 100, "Certificate renewed");
                    // Update metadata
                    auto meta_result = store_.load_metadata(site_id);
                    if (meta_result.success) {
                        auto m = meta_result.metadata;
                        m.renew_attempts = 0;
                        m.last_error = "";
                        m.issued_at = CertificateStore::timestamp_utc();
                        m.updated_at = m.issued_at;
                        store_.save_metadata(site_id, m);
                    }
                    logger_.info("Renewal", "Renewal succeeded for site "
                                 + std::to_string(site_id) + " (" + domain + ")");
                } else {
                    jm.update(jid, "failed", 100, result.message);
                    // Backoff and update metadata
                    auto meta_result = store_.load_metadata(site_id);
                    if (meta_result.success) {
                        auto m = meta_result.metadata;
                        m.renew_attempts++;
                        m.last_error = result.message;
                        m.last_validation = "";
                        store_.save_metadata(site_id, m);
                    }

                    if (meta_result.success) {
                        auto m = meta_result.metadata;
                        if (m.renew_attempts >= 7) {
                            m.status = "error";
                            m.last_error = "Auto-renewal failed after 7 attempts";
                            store_.save_metadata(site_id, m);
                            logger_.warning("Renewal", "Disabled auto-renew for site "
                                           + std::to_string(site_id) + " after 7 failures");
                        }
                    }

                    logger_.warning("Renewal", "Renewal failed for site "
                                    + std::to_string(site_id) + " (" + domain
                                    + "): " + result.message);
                }
            });

        if (!submitted) {
            job_manager_.update(job_id, "failed", 0, "Task queue full");
            logger_.warning("Renewal", "Task queue full, renewal skipped for site "
                           + std::to_string(site_id));
            failed++;
        } else {
            logger_.info("Renewal", "Renewal queued for site " + std::to_string(site_id)
                         + " (" + domain + ")");
            renewed++;
        }
    }

    std::ostringstream log_msg;
    log_msg << "Renewal check complete: " << checked << " checked, "
            << renewed << " renewed, " << skipped << " skipped, "
            << failed << " failed";
    logger_.info("Renewal", log_msg.str());
}

void RenewalScheduler::scheduler_loop() {
    while (running_) {
        check_now();

        // Update next check timestamp
        auto now = std::chrono::system_clock::now();
        auto next = now + interval_;
        auto tt = std::chrono::system_clock::to_time_t(next);
        struct tm tm_buf;
        gmtime_r(&tt, &tm_buf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        next_check_at_ = std::string(buf);

        // Sleep for the interval (in 1-second increments for responsive shutdown)
        auto wakeup = std::chrono::steady_clock::now() + interval_;
        while (running_ && std::chrono::steady_clock::now() < wakeup) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

int RenewalScheduler::backoff_seconds(int attempt) const {
    // Exponential backoff: 1h, 2h, 4h, 8h, 16h, 24h, 24h...
    int hours = std::min(1 << attempt, 24);
    return hours * 3600;
}

} // namespace containercp::ssl
