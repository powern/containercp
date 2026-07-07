#ifndef CONTAINERCP_DAEMON_UNIX_SOCKET_CLIENT_H
#define CONTAINERCP_DAEMON_UNIX_SOCKET_CLIENT_H

#include <string>

namespace containercp::daemon {

class UnixSocketClient {
public:
    explicit UnixSocketClient(const std::string& socket_path);
    ~UnixSocketClient();

    bool connect();
    std::string send_and_receive(const std::string& data);
    void disconnect();

private:
    std::string socket_path_;
    int fd_ = -1;
};

} // namespace containercp::daemon

#endif // CONTAINERCP_DAEMON_UNIX_SOCKET_CLIENT_H
