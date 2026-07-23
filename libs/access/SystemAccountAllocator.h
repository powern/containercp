#ifndef CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_ALLOCATOR_H
#define CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_ALLOCATOR_H

#include "access/SystemAccountMapping.h"

#include <functional>
#include <set>
#include <vector>

namespace containercp::access {

// Allocates monotonic, non-reused UIDs and GIDs within configured ranges.
// Consults both SQLite mappings and actual OS state.
class SystemAccountAllocator {
public:
    struct Range {
        int min = 0;
        int max = 0;
    };

    struct AllocationResult {
        bool success = false;
        int  uid = 0;
        int  gid = 0;
        std::string error;
    };

    // Callbacks for observing existing IDs.
    // Return true if the ID is already occupied.
    using IdOccupiedFn = std::function<bool(int id)>;

    SystemAccountAllocator(Range uid_range, Range gid_range);

    // Allocate fresh UID and GID. Neither value is ever reused.
    AllocationResult allocate(IdOccupiedFn uid_occupied,
                              IdOccupiedFn gid_occupied,
                              const std::vector<SystemAccountMapping>& persisted);

private:
    int next_free(IdOccupiedFn occupied, const std::set<int>& persisted_ids,
                  int min_val, int max_val);

    Range uid_range_;
    Range gid_range_;
};

} // namespace containercp::access

#endif
