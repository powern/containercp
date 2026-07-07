#ifndef CONTAINERCP_STORAGE_STORAGE_H
#define CONTAINERCP_STORAGE_STORAGE_H

#include "node/Node.h"
#include "site/Site.h"

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

private:
    std::string nodes_file() const;
    std::string sites_file() const;

    std::string db_path_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_STORAGE_H
