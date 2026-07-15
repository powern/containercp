#ifndef CONTAINERCP_MAIL_SITE_MAIL_ORCHESTRATOR_H
#define CONTAINERCP_MAIL_SITE_MAIL_ORCHESTRATOR_H

#include "core/OperationResult.h"
#include "mail/SiteMailCredentials.h"
#include "runtime/Runtime.h"
#include "filesystem/Filesystem.h"
#include "config/Config.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <string>

namespace containercp::mail {

struct SiteMailStatus {
    bool enabled = false;
    std::string username;
    std::string envelope_sender;
    bool credential_exists = false;
    bool msmtprc_exists = false;
    bool network_connected = false;
};

class SiteMailOrchestrator {
public:
    SiteMailOrchestrator(SiteMailCredentials& credentials,
                         runtime::Runtime& rt,
                         filesystem::Filesystem& fs,
                         config::Config& cfg);

    core::OperationResult enable_mail(uint64_t site_id,
                                       const std::string& domain,
                                       const std::string& envelope_sender = "");

    core::OperationResult disable_mail(uint64_t site_id,
                                        const std::string& domain = "");

    SiteMailStatus get_status(uint64_t site_id, const std::string& domain,
                               bool enabled);

private:
    SiteMailCredentials& credentials_;
    runtime::Runtime& rt_;
    filesystem::Filesystem& fs_;
    config::Config& cfg_;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_SITE_MAIL_ORCHESTRATOR_H
