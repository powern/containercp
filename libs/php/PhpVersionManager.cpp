#include "PhpVersionManager.h"

namespace containercp::php {

uint64_t PhpVersionManager::create(const std::string& version, const std::string& image, bool default_version) {
    PhpVersion pv;
    pv.id = next_id_++;
    pv.name = version;
    pv.version = version;
    pv.image = image;
    pv.enabled = true;
    pv.default_version = default_version;
    versions_.push_back(std::move(pv));
    return pv.id;
}

PhpVersion* PhpVersionManager::find(const std::string& version) {
    for (auto& pv : versions_) {
        if (pv.version == version) {
            return &pv;
        }
    }
    return nullptr;
}

PhpVersion* PhpVersionManager::find(uint64_t id) {
    for (auto& pv : versions_) {
        if (pv.id == id) {
            return &pv;
        }
    }
    return nullptr;
}

PhpVersion* PhpVersionManager::get_default() {
    for (auto& pv : versions_) {
        if (pv.default_version) {
            return &pv;
        }
    }
    if (!versions_.empty()) {
        return &versions_[0];
    }
    return nullptr;
}

const std::vector<PhpVersion>& PhpVersionManager::list() const {
    return versions_;
}

void PhpVersionManager::set_versions(const std::vector<PhpVersion>& versions) {
    versions_ = versions;
    next_id_ = 1;
    for (const auto& pv : versions_) {
        if (pv.id >= next_id_) {
            next_id_ = pv.id + 1;
        }
    }
}

} // namespace containercp::php
