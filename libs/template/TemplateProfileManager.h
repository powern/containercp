#ifndef CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_MANAGER_H
#define CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_MANAGER_H

#include "template/TemplateProfile.h"

#include <string>
#include <vector>

namespace containercp::template_engine {

class TemplateProfileManager {
public:
    uint64_t create(const std::string& profile_name, const std::string& web_server,
                    const std::string& template_path, const std::string& description,
                    bool default_profile);
    bool remove(uint64_t id);
    TemplateProfile* find(const std::string& profile_name);
    TemplateProfile* find(uint64_t id);
    TemplateProfile* get_default();
    const std::vector<TemplateProfile>& list() const;

    void set_profiles(const std::vector<TemplateProfile>& profiles);

private:
    std::vector<TemplateProfile> profiles_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::template_engine

#endif // CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_MANAGER_H
