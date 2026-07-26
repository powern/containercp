#ifndef CONTAINERCP_ACCESS_SFTP_RUNTIME_STATE_H
#define CONTAINERCP_ACCESS_SFTP_RUNTIME_STATE_H

#include "core/OperationResult.h"

#include <cstddef>
#include <string>
#include <vector>

namespace containercp::access {

// Runtime health state of the SFTP provider.
// Transition: Disabled → Starting → Healthy | Degraded | Failed
enum class SftpRuntimeState {
    Disabled,   // Deps missing or explicitly disabled
    Starting,   // Deps present, reconciliation in progress
    Healthy,    // All reconciliation steps completed successfully
    Degraded,   // Recoverable reconciliation failure — mutations limited
    Failed      // Unrecoverable state — all SFTP operations rejected
};

// Human-readable label for each state.
inline const char* sftp_runtime_state_label(SftpRuntimeState s) {
    switch (s) {
        case SftpRuntimeState::Disabled: return "disabled";
        case SftpRuntimeState::Starting: return "starting";
        case SftpRuntimeState::Healthy:  return "healthy";
        case SftpRuntimeState::Degraded: return "degraded";
        case SftpRuntimeState::Failed:   return "failed";
    }
    return "unknown";
}

// Structured result of a reconciliation step or full startup.
// Never rely on message string matching — use typed fields.
struct ReconciliationResult {
    bool     success = false;
    bool     recoverable = false;
    size_t   records_inspected = 0;
    size_t   records_fixed = 0;
    size_t   records_failed = 0;
    std::vector<std::string> errors;           // bounded error tokens
    std::vector<std::string> affected_identities; // user/site identifiers
    bool     unsafe_foreign_state_detected = false;

    core::OperationResult to_operation_result() const {
        core::OperationResult out;
        out.success = success;
        std::string msg;
        if (!errors.empty()) {
            for (size_t i = 0; i < errors.size() && i < 5; ++i) {
                if (!msg.empty()) msg += "; ";
                msg += errors[i];
            }
            if (errors.size() > 5) msg += "; ...";
        }
        if (records_failed > 0) {
            out.message = std::to_string(records_failed) + " failed, "
                        + std::to_string(records_fixed) + " fixed";
            if (!msg.empty()) out.message += " — " + msg;
        } else {
            out.message = std::to_string(records_fixed) + " fixed";
            if (!msg.empty()) out.message += " — " + msg;
        }
        if (unsafe_foreign_state_detected) {
            if (!out.message.empty()) out.message += "; ";
            out.message += "foreign_state_detected";
        }
        return out;
    }
};

} // namespace containercp::access

#endif
