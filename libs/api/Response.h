#ifndef CONTAINERCP_API_RESPONSE_H
#define CONTAINERCP_API_RESPONSE_H

#include <string>
#include <unordered_map>

namespace containercp::api {

struct Response {
    int status_code = 200;
    std::string body;
    std::string content_type = "application/json";
    std::unordered_map<std::string, std::string> headers;

    std::string to_string() const;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_RESPONSE_H
