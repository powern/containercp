#include "TemplateEngine.h"

namespace containercp::template_engine {

static void replace_all(std::string& result, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string TemplateEngine::render(const std::string& template_content,
                                    const std::string& domain,
                                    const std::string& owner,
                                    const std::string& php_image,
                                    const std::string& site_id) const {
    std::string result = template_content;
    replace_all(result, "{{DOMAIN}}", domain);
    replace_all(result, "{{OWNER}}", owner);
    replace_all(result, "{{PHP_IMAGE}}", php_image);
    replace_all(result, "{{SITE_ID}}", site_id);
    return result;
}

std::string TemplateEngine::render_web(const std::string& template_content,
                                        const std::string& domain,
                                        const std::string& public_root,
                                        const std::string& php_upstream,
                                        const std::string& log_root,
                                        bool ssl_enabled) const {
    std::string result = template_content;
    replace_all(result, "{{DOMAIN}}", domain);
    replace_all(result, "{{PUBLIC_ROOT}}", public_root);
    replace_all(result, "{{PHP_UPSTREAM}}", php_upstream);
    replace_all(result, "{{LOG_ROOT}}", log_root);
    replace_all(result, "{{SSL_ENABLED}}", ssl_enabled ? "true" : "false");
    return result;
}

} // namespace containercp::template_engine
