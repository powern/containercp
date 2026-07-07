#ifndef CONTAINERCP_DATABASE_DATABASE_MANAGER_H
#define CONTAINERCP_DATABASE_DATABASE_MANAGER_H

#include "database/Database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::database {

class DatabaseManager {
public:
    uint64_t create(const std::string& db_name, const std::string& db_user, const std::string& db_password, uint64_t owner_id, uint64_t site_id);
    bool remove(uint64_t id);
    Database* find(uint64_t id);
    Database* find(const std::string& db_name);
    const std::vector<Database>& list() const;

    void set_databases(const std::vector<Database>& databases);

private:
    std::vector<Database> databases_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_MANAGER_H
