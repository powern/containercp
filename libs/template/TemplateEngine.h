#ifndef CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H
#define CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H

#include <string>

namespace containercp::template_engine {

class TemplateEngine {
public:
    std::string render(const std::string& template_content,
                       const std::string& domain,
                       const std::string& owner,
                       const std::string& php_image,
                       const std::string& site_id = "") const;

    std::string render_web(const std::string& template_content,
                           const std::string& domain,
                           const std::string& public_root,
                           const std::string& php_upstream,
                           const std::string& log_root,
                           bool ssl_enabled) const;
};

} // namespace containercp::template_engine

#endif // CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H
