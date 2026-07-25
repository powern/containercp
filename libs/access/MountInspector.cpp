#include "access/MountInspector.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace containercp::access {
namespace {

bool decode_mountinfo_escape(std::string& field) {
    std::string out;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            char d1 = field[i + 1];
            char d2 = field[i + 2];
            char d3 = field[i + 3];
            if (d1 == '0' && d2 == '4' && d3 == '0') { out += ' ';  i += 3; }
            else if (d1 == '0' && d2 == '1' && d3 == '1') { out += '\t'; i += 3; }
            else if (d1 == '0' && d2 == '1' && d3 == '2') { out += '\n'; i += 3; }
            else if (d1 == '1' && d2 == '3' && d3 == '4') { out += '\\'; i += 3; }
            else { out += field[i]; }
        } else {
            out += field[i];
        }
    }
    field = out;
    return true;
}

// Split on space, respecting \040 escape sequences
std::vector<std::string> tokenize_mountinfo(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 3 < line.size()) {
            char d1 = line[i + 1], d2 = line[i + 2], d3 = line[i + 3];
            if ((d1 == '0' && d2 == '4' && d3 == '0') ||
                (d1 == '0' && d2 == '1' && (d3 == '1' || d3 == '2')) ||
                (d1 == '1' && d2 == '3' && d3 == '4')) {
                cur += line[i];
                cur += line[i + 1];
                cur += line[i + 2];
                cur += line[i + 3];
                i += 3;
                continue;
            }
        }
        if (line[i] == ' ') {
            tokens.push_back(cur);
            cur.clear();
        } else {
            cur += line[i];
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

} // namespace

MountState parse_mountinfo_line(const std::string& line, const std::string& target_filter) {
    MountState s;

    // Find the " - " separator
    auto dash = line.find(" - ");
    if (dash == std::string::npos) {
        s.status = MountStatus::Absent;
        s.error_detail = "missing separator";
        return s;
    }

    auto before_str = line.substr(0, dash);
    auto after_str  = line.substr(dash + 3);

    auto before_tokens = tokenize_mountinfo(before_str);
    auto after_tokens  = tokenize_mountinfo(after_str);

    // Before separator: id parent_id major:minor root mountpoint options [optional_fields...]
    // Minimum: 6 tokens (id, parent_id, major:minor, root, mountpoint, options)
    if (before_tokens.size() < 6) {
        s.status = MountStatus::Absent;
        s.error_detail = "too few fields before separator";
        return s;
    }

    // After separator: fstype source super_options
    if (after_tokens.size() < 3) {
        s.status = MountStatus::Absent;
        s.error_detail = "too few fields after separator";
        return s;
    }

    // Parse mount_id, parent_id
    char* end = nullptr;
    s.mount_id = static_cast<uint64_t>(std::strtoull(before_tokens[0].c_str(), &end, 10));
    if (!end || *end != '\0') {
        s.status = MountStatus::Absent;
        s.error_detail = "invalid mount_id";
        return s;
    }
    s.parent_id = static_cast<uint64_t>(std::strtoull(before_tokens[1].c_str(), &end, 10));
    if (!end || *end != '\0') {
        s.status = MountStatus::Absent;
        s.error_detail = "invalid parent_id";
        return s;
    }

    s.device = before_tokens[2];
    s.bind_root = before_tokens[3];

    // Decode escapes in root, mountpoint, source
    decode_mountinfo_escape(s.bind_root);
    decode_mountinfo_escape(before_tokens[4]);
    if (!target_filter.empty() && before_tokens[4] != target_filter) {
        s.status = MountStatus::Absent;
        s.error_detail = "target mismatch";
        return s;
    }
    s.target = before_tokens[4];

    // Options are token 5
    s.options = before_tokens[5];

    // Optional fields: everything between token 6 and the end of before_tokens
    for (std::size_t i = 6; i < before_tokens.size(); ++i) {
        s.optional_fields.push_back(before_tokens[i]);
    }

    // After separator: fstype source super_options
    s.fstype = after_tokens[0];
    s.source = after_tokens[1];
    decode_mountinfo_escape(s.source);

    // Super options — everything from token 2 onward may contain spaces
    for (std::size_t i = 2; i < after_tokens.size(); ++i) {
        if (i > 2) s.super_options += ' ';
        s.super_options += after_tokens[i];
    }

    s.mounted = true;

    // Bind mount determination: root != "/" OR source is not a block device path
    // Real bind mounts have a non-"root" root AND source is a real filesystem path (not a device like /dev/sda1)
    bool root_is_subdir = (s.bind_root != "/");
    bool source_is_path = (s.source.find('/') == 0);
    bool source_is_device = (!source_is_path && s.source.find('/') == std::string::npos && !s.source.empty());
    s.is_bind = root_is_subdir;

    s.status = MountStatus::Ok;
    return s;
}

MountState parse_mountinfo(const std::string& content, const std::string& target) {
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        auto s = parse_mountinfo_line(line, target);
        if (s.status == MountStatus::Ok) return s;
    }
    MountState s;
    s.status = MountStatus::Absent;
    s.error_detail = "target not found in mountinfo";
    return s;
}

std::vector<MountState> enumerate_mountinfo(const std::string& content, const std::string& root_prefix) {
    std::vector<MountState> result;
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        auto s = parse_mountinfo_line(line);
        if (s.status == MountStatus::Ok && s.target.rfind(root_prefix, 0) == 0) {
            result.push_back(std::move(s));
        }
    }
    return result;
}

class RealMountInspector : public MountInspector {
public:
    RealMountInspector() = default;

    MountState inspect(const std::string& path) const override {
        std::ifstream file("/proc/self/mountinfo");
        if (!file) {
            MountState s;
            s.status = MountStatus::InspectionFailed;
            s.error_detail = "cannot open /proc/self/mountinfo";
            return s;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        if (!file) {
            MountState s;
            s.status = MountStatus::InspectionFailed;
            s.error_detail = "cannot read /proc/self/mountinfo";
            return s;
        }
        return parse_mountinfo(buffer.str(), path);
    }

    std::vector<MountState> enumerate(const std::string& root_prefix) const override {
        std::ifstream file("/proc/self/mountinfo");
        if (!file) return {};
        std::stringstream buffer;
        buffer << file.rdbuf();
        if (!file) return {};
        return enumerate_mountinfo(buffer.str(), root_prefix);
    }
};

std::shared_ptr<MountInspector>
make_real_mount_inspector() {
    return std::make_shared<RealMountInspector>();
}

} // namespace containercp::access
