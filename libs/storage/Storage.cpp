#include "Storage.h"

#include <fstream>
#include <sstream>
#include <string>

namespace containercp::storage {

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
}

std::string Storage::nodes_file() const {
    return db_path_ + "nodes.db";
}

std::string Storage::sites_file() const {
    return db_path_ + "sites.db";
}

std::string Storage::users_file() const {
    return db_path_ + "users.db";
}

std::string Storage::domains_file() const {
    return db_path_ + "domains.db";
}

void Storage::save_nodes(const std::vector<node::Node>& nodes) {
    std::ofstream file(nodes_file());
    for (const auto& n : nodes) {
        file << n.id << "|" << n.name << "|" << n.type << "\n";
    }
}

std::vector<node::Node> Storage::load_nodes() {
    std::vector<node::Node> nodes;
    std::ifstream file(nodes_file());
    if (!file.is_open()) {
        return nodes;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        node::Node n;
        if (std::getline(ss, token, '|')) n.id = std::stoull(token);
        if (std::getline(ss, token, '|')) n.name = token;
        if (std::getline(ss, token, '|')) n.type = token;
        nodes.push_back(std::move(n));
    }
    return nodes;
}

void Storage::save_sites(const std::vector<site::Site>& sites) {
    std::ofstream file(sites_file());
    for (const auto& s : sites) {
        file << s.id << "|" << s.domain << "|" << s.owner << "|" << s.node_id << "\n";
    }
}

std::vector<site::Site> Storage::load_sites() {
    std::vector<site::Site> sites;
    std::ifstream file(sites_file());
    if (!file.is_open()) {
        return sites;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        site::Site s;
        if (std::getline(ss, token, '|')) s.id = std::stoull(token);
        if (std::getline(ss, token, '|')) s.domain = token;
        if (std::getline(ss, token, '|')) s.owner = token;
        if (std::getline(ss, token, '|')) s.node_id = std::stoull(token);
        s.name = s.domain;
        sites.push_back(std::move(s));
    }
    return sites;
}

void Storage::save_users(const std::vector<user::User>& users) {
    std::ofstream file(users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.uid << "|"
             << u.home_directory << "|" << u.shell << "|" << (u.enabled ? "1" : "0") << "\n";
    }
}

std::vector<user::User> Storage::load_users() {
    std::vector<user::User> users;
    std::ifstream file(users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        user::User u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.uid = std::stoull(token);
        if (std::getline(ss, token, '|')) u.home_directory = token;
        if (std::getline(ss, token, '|')) u.shell = token;
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_domains(const std::vector<domain::Domain>& domains) {
    std::ofstream file(domains_file());
    for (const auto& d : domains) {
        file << d.id << "|" << d.fqdn << "|" << d.owner_id << "|"
             << d.site_id << "|" << d.php_version << "|"
             << (d.ssl_enabled ? "1" : "0") << "|" << (d.enabled ? "1" : "0") << "\n";
    }
}

std::vector<domain::Domain> Storage::load_domains() {
    std::vector<domain::Domain> domains;
    std::ifstream file(domains_file());
    if (!file.is_open()) {
        return domains;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        domain::Domain d;
        if (std::getline(ss, token, '|')) d.id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.fqdn = token;
        if (std::getline(ss, token, '|')) d.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.php_version = token;
        if (std::getline(ss, token, '|')) d.ssl_enabled = (token == "1");
        if (std::getline(ss, token, '|')) d.enabled = (token == "1");
        d.name = d.fqdn;
        domains.push_back(std::move(d));
    }
    return domains;
}

} // namespace containercp::storage
