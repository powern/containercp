#ifndef CONTAINERCP_CONFIG_CONFIG_H
#define CONTAINERCP_CONFIG_CONFIG_H

#include <string>

namespace containercp::config {

class Config {
public:
    static Config& instance();

    const std::string& source_root() const;
    const std::string& config_root() const;
    const std::string& data_root() const;
    const std::string& log_root() const;

    std::string database_dir() const;
    std::string sites_dir() const;
    std::string templates_dir() const;
    std::string users_dir() const;
    std::string proxy_dir() const;
    std::string web_templates_dir() const;

    std::string server_hostname() const;
    void set_server_hostname(const std::string& hostname);
    void load_server_hostname();
    void save_server_hostname() const;

private:
    Config();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string source_root_;
    std::string config_root_;
    std::string data_root_;
    std::string log_root_;
    std::string server_hostname_;
};

} // namespace containercp::config

#endif // CONTAINERCP_CONFIG_CONFIG_H
