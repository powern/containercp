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

} // namespace containercp::storage
