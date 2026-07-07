#ifndef CONTAINERCP_CLI_COMMAND_DISPATCHER_H
#define CONTAINERCP_CLI_COMMAND_DISPATCHER_H

namespace containercp::cli {

class CommandDispatcher {
public:
    static int run(int argc, char* argv[]);
};

} // namespace containercp::cli

#endif // CONTAINERCP_CLI_COMMAND_DISPATCHER_H
