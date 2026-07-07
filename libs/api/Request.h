#ifndef CONTAINERCP_API_REQUEST_H
#define CONTAINERCP_API_REQUEST_H

#include <string>
#include <unordered_map>

namespace containercp::api {

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> headers;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_REQUEST_H
