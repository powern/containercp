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
                                    const std::string& site_id,
                                    const std::string& web_server_image,
                                    const std::string& web_config_dir,
                                    const std::string& web_log_dir,
                                    const std::string& web_doc_root,
                                    const std::string& web_local_config,
                                    const std::string& web_local_log,
                                    const std::string& web_server_cmd) const {
    std::string result = template_content;

    std::string web_health_cmd = "nginx";
    if (web_server_image.find("httpd") != std::string::npos) {
        web_health_cmd = "httpd";
    }

    // Build command line: for nginx use default, for httpd inject extra config include
    std::string cmd_line;
    if (!web_server_cmd.empty()) {
        cmd_line = "    command: " + web_server_cmd + "\n";
    }

    replace_all(result, "{{DOMAIN}}", domain);
    replace_all(result, "{{OWNER}}", owner);
    replace_all(result, "{{PHP_IMAGE}}", php_image);
    replace_all(result, "{{SITE_ID}}", site_id);
    replace_all(result, "{{WEB_SERVER_IMAGE}}", web_server_image);
    replace_all(result, "{{WEB_CONFIG_DIR}}", web_config_dir);
    replace_all(result, "{{WEB_LOG_DIR}}", web_log_dir);
    replace_all(result, "{{WEB_DOC_ROOT}}", web_doc_root);
    replace_all(result, "{{WEB_HEALTH_CMD}}", web_health_cmd);
    replace_all(result, "{{WEB_LOCAL_CONFIG}}", web_local_config);
    replace_all(result, "{{WEB_LOCAL_LOG}}", web_local_log);
    replace_all(result, "{{WEB_SERVER_CMD}}", cmd_line);
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
