#include "TemplateProfileManager.h"

namespace containercp::template_engine {

uint64_t TemplateProfileManager::create(const std::string& profile_name, const std::string& web_server,
                                         const std::string& template_path, const std::string& description,
                                         bool default_profile) {
    TemplateProfile p;
    p.id = next_id_++;
    p.name = profile_name;
    p.profile_name = profile_name;
    p.web_server = web_server;
    p.runtime = "docker";
    p.template_path = template_path;
    p.description = description;
    p.enabled = true;
    p.default_profile = default_profile;
    profiles_.push_back(std::move(p));
    return p.id;
}

bool TemplateProfileManager::remove(uint64_t id) {
    for (auto it = profiles_.begin(); it != profiles_.end(); ++it) {
        if (it->id == id) {
            profiles_.erase(it);
            return true;
        }
    }
    return false;
}

TemplateProfile* TemplateProfileManager::find(const std::string& profile_name) {
    for (auto& p : profiles_) {
        if (p.profile_name == profile_name) {
            return &p;
        }
    }
    return nullptr;
}

TemplateProfile* TemplateProfileManager::find(uint64_t id) {
    for (auto& p : profiles_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

TemplateProfile* TemplateProfileManager::get_default() {
    for (auto& p : profiles_) {
        if (p.default_profile) {
            return &p;
        }
    }
    if (!profiles_.empty()) return &profiles_[0];
    return nullptr;
}

const std::vector<TemplateProfile>& TemplateProfileManager::list() const {
    return profiles_;
}

void TemplateProfileManager::set_profiles(const std::vector<TemplateProfile>& profiles) {
    profiles_ = profiles;
    next_id_ = 1;
    for (const auto& p : profiles_) {
        if (p.id >= next_id_) {
            next_id_ = p.id + 1;
        }
    }
}

} // namespace containercp::template_engine
