#ifndef CONTAINERCP_PROFILE_PROFILE_MANAGER_H
#define CONTAINERCP_PROFILE_PROFILE_MANAGER_H

#include "profile/Profile.h"

#include <string>
#include <vector>

namespace containercp::profile {

class ProfileManager {
public:
    uint64_t create(const std::string& profile_name, ProfileType type,
                    const std::string& web_server, const std::string& template_path,
                    const std::string& description, bool default_profile);
    bool remove(uint64_t id);
    Profile* find(const std::string& profile_name);
    Profile* find(uint64_t id);
    Profile* get_default(ProfileType type);
    std::vector<Profile*> list_by_type(ProfileType type);
    const std::vector<Profile>& list() const;

    void set_profiles(const std::vector<Profile>& profiles);

private:
    std::vector<Profile> profiles_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::profile

#endif // CONTAINERCP_PROFILE_PROFILE_MANAGER_H
