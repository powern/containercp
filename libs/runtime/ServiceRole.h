#ifndef CONTAINERCP_RUNTIME_SERVICE_ROLE_H
#define CONTAINERCP_RUNTIME_SERVICE_ROLE_H

#include <string>
#include <vector>

namespace containercp::runtime {

// Runtime service roles abstract away implementation details.
//
// A role represents what a service DOES, not what software it runs.
// Example: Frontend is always "web" in compose regardless of Apache/Nginx.
//
// Future roles: Mail, Queue, Worker, Scheduler, Proxy.
enum class ServiceRole {
    Frontend,   // web server (Apache, Nginx, LiteSpeed, …)
    PHP,        // PHP-FPM
    Database,   // MariaDB, PostgreSQL, …
    Cache,      // Redis, Memcached, …
};

// ── Role → action string ──────────────────────────────────────────
// Convert a role to the action suffix used in API endpoints.
// Frontend → "web", PHP → "php", Database → "db", Cache → "redis".
std::string role_to_action_suffix(ServiceRole role);

// ── Action string → roles ─────────────────────────────────────────
// "restart-web"    → {Frontend}
// "restart-php"    → {PHP}
// "restart-db"     → {Database}
// "restart-redis"  → {Cache}
// "restart-all"    → {Frontend, PHP, Database, Cache}
// Returns empty vector for unknown actions.
std::vector<ServiceRole> roles_from_action(const std::string& action);

// ── Role → compose service name ───────────────────────────────────
// Frontend  → "web"
// PHP       → "php"
// Database  → "mariadb"
// Cache     → "redis"
std::string role_to_compose_service(ServiceRole role);

// Convenience: convert multiple roles to compose service names.
// Returns empty vector if roles is empty (signals "all services").
std::vector<std::string> roles_to_compose_services(const std::vector<ServiceRole>& roles);

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_SERVICE_ROLE_H
