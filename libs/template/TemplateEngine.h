#ifndef CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H
#define CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H

#include <string>

namespace containercp::template_engine {

class TemplateEngine {
public:
    std::string render(const std::string& template_content, const std::string& domain, const std::string& owner) const;
};

} // namespace containercp::template_engine

#endif // CONTAINERCP_TEMPLATE_TEMPLATE_ENGINE_H
