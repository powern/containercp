#ifndef CONTAINERCP_RUNTIME_RUNTIME_H
#define CONTAINERCP_RUNTIME_RUNTIME_H

#include "core/OperationResult.h"

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
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_H
