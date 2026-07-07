#include "UnixSocketClient.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace containercp::daemon {

UnixSocketClient::UnixSocketClient(const std::string& socket_path)
    : socket_path_(socket_path)
{
}

UnixSocketClient::~UnixSocketClient() {
    disconnect();
}

bool UnixSocketClient::connect() {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

std::string UnixSocketClient::send_and_receive(const std::string& data) {
    if (fd_ < 0) return "ERROR|Not connected";

    ::write(fd_, data.c_str(), data.size());
    ::shutdown(fd_, SHUT_WR);

    char buf[65536];
    ssize_t n = ::read(fd_, buf, sizeof(buf) - 1);
    if (n <= 0) return "ERROR|No response";

    buf[n] = '\0';
    return std::string(buf);
}

void UnixSocketClient::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace containercp::daemon
