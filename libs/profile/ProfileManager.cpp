#include "ProfileManager.h"

namespace containercp::profile {

uint64_t ProfileManager::create(const std::string& profile_name, ProfileType type,
                                 const std::string& web_server, const std::string& template_path,
                                 const std::string& description, bool default_profile) {
    Profile p;
    p.id = next_id_++;
    p.name = profile_name;
    p.profile_name = profile_name;
    p.type = type;
    p.web_server = web_server;
    p.runtime = "docker";
    p.template_path = template_path;
    p.description = description;
    p.enabled = true;
    p.default_profile = default_profile;
    profiles_.push_back(std::move(p));
    return p.id;
}

bool ProfileManager::remove(uint64_t id) {
    for (auto it = profiles_.begin(); it != profiles_.end(); ++it) {
        if (it->id == id) {
            profiles_.erase(it);
            return true;
        }
    }
    return false;
}

Profile* ProfileManager::find(const std::string& profile_name) {
    for (auto& p : profiles_) {
        if (p.profile_name == profile_name) {
            return &p;
        }
    }
    return nullptr;
}

Profile* ProfileManager::find(uint64_t id) {
    for (auto& p : profiles_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

Profile* ProfileManager::get_default(ProfileType type) {
    for (auto& p : profiles_) {
        if (p.type == type && p.default_profile) {
            return &p;
        }
    }
    for (auto& p : profiles_) {
        if (p.type == type) return &p;
    }
    return nullptr;
}

std::vector<Profile*> ProfileManager::list_by_type(ProfileType type) {
    std::vector<Profile*> result;
    for (auto& p : profiles_) {
        if (p.type == type) {
            result.push_back(&p);
        }
    }
    return result;
}

const std::vector<Profile>& ProfileManager::list() const {
    return profiles_;
}

void ProfileManager::set_profiles(const std::vector<Profile>& profiles) {
    profiles_ = profiles;
    next_id_ = 1;
    for (const auto& p : profiles_) {
        if (p.id >= next_id_) {
            next_id_ = p.id + 1;
        }
    }
}

} // namespace containercp::profile
