#include "RuntimeSynchronizer.h"

namespace containercp::runtime {

void RuntimeSynchronizer::register_handler(const std::string& name,
                                            SyncCallback callback) {
    handlers_[name] = std::move(callback);
}

core::OperationResult RuntimeSynchronizer::sync(const std::string& name) {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        return {false, "No sync handler registered for: " + name};
    }
    return it->second();
}

} // namespace containercp::runtime
