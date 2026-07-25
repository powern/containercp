#include "access/MountInspector.h"

#include "runtime/CommandExecutor.h"

#include <sstream>
#include <vector>

namespace containercp::access {
namespace {

MountState parse_mountinfo(const std::string& output, const std::string& target) {
    MountState s;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        // /proc/self/mountinfo format:
        // id parent_id major:minor root mountpoint options [optional_fields] - fstype source superopts
        // Fields 0-3: id, parent_id, dev:dev, root
        // Field  4:   mountpoint
        // Fields 5+:  options + optional fields (up to " - ")
        // After " - ": fstype source superopts
        auto pos = line.find(" - ");
        if (pos == std::string::npos) continue;
        auto before = line.substr(0, pos);
        auto after  = line.substr(pos + 3);

        std::vector<std::string> tokens;
        std::istringstream bs(before);
        std::string tok;
        while (bs >> tok) tokens.push_back(tok);

        if (tokens.size() < 5) continue;

        auto& root       = tokens[3];
        auto& mountpoint = tokens[4];

        if (mountpoint != target) continue;

        s.mounted   = true;
        s.target    = mountpoint;
        s.bind_root = root;
        s.is_bind   = (root != "/");

        std::istringstream rs(after);
        std::string fstype, source;
        rs >> fstype >> source;
        s.fstype = fstype;
        s.source = source;
        s.status = MountStatus::Ok;
        return s;
    }
    s.status = MountStatus::Absent;
    return s;
}

} // namespace

class RealMountInspector : public MountInspector {
public:
    explicit RealMountInspector(runtime::CommandExecutor& executor) : executor_(executor) {}

    MountState inspect(const std::string& path) const override {
        auto result = executor_.run({"cat", "/proc/self/mountinfo"});
        if (result.exit_code != 0) {
            MountState s;
            s.status = MountStatus::InspectionFailed;
            s.error_detail = "cannot read mountinfo";
            return s;
        }
        return parse_mountinfo(result.out, path);
    }

private:
    runtime::CommandExecutor& executor_;
};

std::shared_ptr<MountInspector>
make_real_mount_inspector(runtime::CommandExecutor& executor) {
    return std::make_shared<RealMountInspector>(executor);
}

} // namespace containercp::access
