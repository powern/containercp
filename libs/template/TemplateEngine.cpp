#include "TemplateEngine.h"

namespace containercp::template_engine {

std::string TemplateEngine::render(const std::string& template_content, const std::string& domain, const std::string& owner, const std::string& php_image) const {
    std::string result = template_content;

    auto replace = [&](const std::string& from, const std::string& to) {
        std::size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
    };

    replace("{{DOMAIN}}", domain);
    replace("{{OWNER}}", owner);
    replace("{{PHP_IMAGE}}", php_image);

    return result;
}

} // namespace containercp::template_engine
