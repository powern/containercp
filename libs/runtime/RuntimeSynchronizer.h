#ifndef CONTAINERCP_RUNTIME_RUNTIME_SYNCHRONIZER_H
#define CONTAINERCP_RUNTIME_RUNTIME_SYNCHRONIZER_H

#include "core/OperationResult.h"

#include <functional>
#include <map>
#include <string>

namespace containercp::runtime {

// Generic runtime synchronization registry.
//
// Each subsystem (Mail, DNS, SSL, Proxy) registers a sync callback.
// When data changes, the API handler calls sync("name") once.
// The mechanism supports future batching (queue + flush) without
// changing the caller API.
//
// Register callbacks during ServiceRegistry construction.
// Call sync() after any data mutation that affects runtime config.
class RuntimeSynchronizer {
public:
    using SyncCallback = std::function<core::OperationResult()>;

    // Register a named sync handler.  Replaces any existing handler
    // with the same name (supports re-registration during tests).
    void register_handler(const std::string& name, SyncCallback callback);

    // Execute the registered sync handler immediately.
    // Returns the handler's result, or an error if no handler is
    // registered for the given name.
    core::OperationResult sync(const std::string& name);

private:
    std::map<std::string, SyncCallback> handlers_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_SYNCHRONIZER_H
