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

    // Run command, write stdout directly to file (no RAM accumulation)
    CommandResult run_stdout_to_file(const std::vector<std::string>& args,
                                     const std::string& output_path,
                                     const std::string& workdir = "") const;

    // Run command, read stdin from file
    CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                     const std::string& input_path,
                                     const std::string& workdir = "") const;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_COMMAND_EXECUTOR_H
