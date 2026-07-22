#include "SecureRandom.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/random.h>
#include <unistd.h>

namespace containercp::security {

namespace {

bool fill_from_getrandom(unsigned char* out, std::size_t count) {
    std::size_t done = 0;
    while (done < count) {
        ssize_t n = ::getrandom(out + done, count - done, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool fill_from_urandom(unsigned char* out, std::size_t count) {
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::size_t done = 0;
    while (done < count) {
        ssize_t n = ::read(fd, out + done, count - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        if (n == 0) {
            ::close(fd);
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return true;
}

} // namespace

std::optional<std::vector<unsigned char>> SecureRandom::bytes(std::size_t count) {
    std::vector<unsigned char> out(count);
    if (count == 0) return out;
    if (fill_from_getrandom(out.data(), out.size())) return out;
    if (fill_from_urandom(out.data(), out.size())) return out;
    return std::nullopt;
}

std::optional<std::string> SecureRandom::hex(std::size_t byte_count) {
    auto data = bytes(byte_count);
    if (!data) return std::nullopt;
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(byte_count * 2);
    for (unsigned char b : *data) {
        out.push_back(hex_chars[(b >> 4) & 0x0f]);
        out.push_back(hex_chars[b & 0x0f]);
    }
    return out;
}

std::optional<std::string> SecureRandom::string(std::size_t length, const std::string& alphabet) {
    if (alphabet.empty()) return std::nullopt;
    if (length == 0) return std::string{};
    if (alphabet.size() > 256) return std::nullopt;

    const auto alphabet_size = static_cast<unsigned int>(alphabet.size());
    const unsigned int limit = 256u - (256u % alphabet_size);
    std::string out;
    out.reserve(length);
    while (out.size() < length) {
        auto block = bytes(length - out.size());
        if (!block) return std::nullopt;
        for (unsigned char b : *block) {
            if (b >= limit) continue;
            out.push_back(alphabet[static_cast<std::size_t>(b % alphabet_size)]);
            if (out.size() == length) break;
        }
    }
    return out;
}

} // namespace containercp::security
