#ifndef CONTAINERCP_RUNTIME_RUNTIME_H
#define CONTAINERCP_RUNTIME_RUNTIME_H

#include <string>

namespace containercp::runtime {

class Runtime {
public:
    virtual ~Runtime() = default;

    virtual void create_site_stack(const std::string& domain) = 0;
    virtual void start_site(const std::string& domain) = 0;
    virtual void stop_site(const std::string& domain) = 0;
    virtual void remove_site(const std::string& domain) = 0;
    virtual void status(const std::string& domain) = 0;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_H
