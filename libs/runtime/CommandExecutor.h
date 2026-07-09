#ifndef CONTAINERCP_RUNTIME_COMMAND_EXECUTOR_H
#define CONTAINERCP_RUNTIME_COMMAND_EXECUTOR_H

#include <string>
#include <vector>

namespace containercp::runtime {

struct CommandResult {
    int exit_code = -1;
    std::string out;
    std::string err;
};

class CommandExecutor {
public:
    CommandResult run(const std::vector<std::string>& args,
                      const std::string& workdir = "") const;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_COMMAND_EXECUTOR_H
