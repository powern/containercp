#include "access/MountInspector.h"

#include "runtime/CommandExecutor.h"

#include <sstream>

namespace containercp::access {
namespace {

MountState parse_mountinfo(const std::string& output, const std::string& target) {
    MountState s;
    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        // /proc/self/mountinfo format:
        // id parent_id dev:dev root mountpoint options ... - fstype source superopts
        auto pos = line.find(" - ");
        if (pos == std::string::npos) continue;
        auto fields = line.substr(0, pos);
        auto rest = line.substr(pos + 3);

        // Extract mountpoint (5th field)
        std::istringstream fs(fields);
        std::string token;
        for (int i = 0; i < 5; ++i) std::getline(fs, token, ' ');
        if (token != target) continue;

        s.mounted = true;
        s.target = token;

        // Extract fstype and source from "- fstype source superopts"
        std::istringstream rs(rest);
        std::string fstype, source;
        rs >> fstype >> source;
        s.fstype = fstype;
        s.source = source;
        s.is_bind = (fstype != "proc" && fstype != "sysfs" && fstype != "devtmpfs");
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
