#include "access/SystemAccountAllocator.h"

#include <algorithm>

namespace containercp::access {

SystemAccountAllocator::SystemAccountAllocator(Range uid_range, Range gid_range)
    : uid_range_(uid_range), gid_range_(gid_range) {}

SystemAccountAllocator::AllocationResult
SystemAccountAllocator::allocate(IdOccupiedFn uid_occupied,
                                 IdOccupiedFn gid_occupied,
                                 const std::vector<SystemAccountMapping>& persisted) {
    AllocationResult result;

    if (uid_range_.min > uid_range_.max || uid_range_.min <= 0 ||
        gid_range_.min > gid_range_.max || gid_range_.min <= 0) {
        result.error = "invalid UID/GID range";
        return result;
    }

    std::set<int> persisted_uids, persisted_gids;
    for (const auto& m : persisted) {
        if (m.uid > 0) persisted_uids.insert(m.uid);
        if (m.gid > 0) persisted_gids.insert(m.gid);
    }

    int uid = next_free(uid_occupied, persisted_uids, uid_range_.min, uid_range_.max);
    if (uid <= 0) {
        result.error = "UID range exhausted";
        return result;
    }

    int gid = next_free(gid_occupied, persisted_gids, gid_range_.min, gid_range_.max);
    if (gid <= 0) {
        result.error = "GID range exhausted";
        return result;
    }

    result.success = true;
    result.uid = uid;
    result.gid = gid;
    return result;
}

int SystemAccountAllocator::next_free(IdOccupiedFn occupied,
                                      const std::set<int>& persisted_ids,
                                      int min_val, int max_val) {
    for (int id = min_val; id <= max_val; ++id) {
        if (persisted_ids.count(id)) continue;
        if (occupied(id)) continue;
        return id;
    }
    return 0;
}

} // namespace containercp::access
