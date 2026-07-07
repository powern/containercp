#ifndef CONTAINERCP_STORAGE_STORAGE_H
#define CONTAINERCP_STORAGE_STORAGE_H

#include "domain/Domain.h"
#include "node/Node.h"
#include "site/Site.h"
#include "user/User.h"

#include <string>
#include <vector>

namespace containercp::storage {

class Storage {
public:
    explicit Storage(const std::string& db_path);

    void save_nodes(const std::vector<node::Node>& nodes);
    std::vector<node::Node> load_nodes();

    void save_sites(const std::vector<site::Site>& sites);
    std::vector<site::Site> load_sites();

    void save_users(const std::vector<user::User>& users);
    std::vector<user::User> load_users();

    void save_domains(const std::vector<domain::Domain>& domains);
    std::vector<domain::Domain> load_domains();

private:
    std::string nodes_file() const;
    std::string sites_file() const;
    std::string users_file() const;
    std::string domains_file() const;

    std::string db_path_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_STORAGE_H
