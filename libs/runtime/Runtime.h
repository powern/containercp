#ifndef CONTAINERCP_RUNTIME_RUNTIME_H
#define CONTAINERCP_RUNTIME_RUNTIME_H

#include "core/OperationResult.h"

#include <cstdint>
#include <string>

namespace containercp::runtime {

class Runtime {
public:
    virtual ~Runtime() = default;

    virtual core::OperationResult create_site_stack(const std::string& domain) = 0;
    virtual core::OperationResult check_compose() = 0;
    virtual core::OperationResult start_site(const std::string& domain) = 0;
    virtual core::OperationResult stop_site(const std::string& domain) = 0;
    virtual core::OperationResult remove_site(const std::string& domain) = 0;
    virtual core::OperationResult status(const std::string& domain) = 0;

    // Mail integration
    virtual core::OperationResult connect_mail_network(uint64_t site_id,
                                                        const std::string& domain) = 0;
    virtual core::OperationResult disconnect_mail_network(uint64_t site_id,
                                                           const std::string& domain) = 0;
    virtual core::OperationResult sync_site_mail(uint64_t site_id) = 0;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_H
