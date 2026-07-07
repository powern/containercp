#ifndef CONTAINERCP_PROFILE_PROFILE_TYPE_H
#define CONTAINERCP_PROFILE_PROFILE_TYPE_H

#include <string>

namespace containercp::profile {

enum class ProfileType {
    WEB_SERVER,
    PHP,
    DOCKER,
    SSL,
    BACKUP,
    MAIL,
    DNS
};

inline std::string profile_type_to_string(ProfileType t) {
    switch (t) {
        case ProfileType::WEB_SERVER: return "web_server";
        case ProfileType::PHP: return "php";
        case ProfileType::DOCKER: return "docker";
        case ProfileType::SSL: return "ssl";
        case ProfileType::BACKUP: return "backup";
        case ProfileType::MAIL: return "mail";
        case ProfileType::DNS: return "dns";
    }
    return "web_server";
}

inline ProfileType profile_type_from_string(const std::string& s) {
    if (s == "php") return ProfileType::PHP;
    if (s == "docker") return ProfileType::DOCKER;
    if (s == "ssl") return ProfileType::SSL;
    if (s == "backup") return ProfileType::BACKUP;
    if (s == "mail") return ProfileType::MAIL;
    if (s == "dns") return ProfileType::DNS;
    return ProfileType::WEB_SERVER;
}

} // namespace containercp::profile

#endif // CONTAINERCP_PROFILE_PROFILE_TYPE_H
