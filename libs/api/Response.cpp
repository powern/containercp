#include "Response.h"

#include <sstream>

namespace containercp::api {

std::string Response::to_string() const {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status_code << " "
         << (status_code == 200 ? "OK" :
              status_code == 201 ? "Created" :
              status_code == 202 ? "Accepted" :
              status_code == 400 ? "Bad Request" :
              status_code == 401 ? "Unauthorized" :
              status_code == 403 ? "Forbidden" :
              status_code == 404 ? "Not Found" :
              status_code == 410 ? "Gone" :
              status_code == 500 ? "Internal Server Error" : "OK")
          << "\r\n"
          << "Content-Type: " << content_type << "\r\n"
          << "Content-Length: " << body.size() << "\r\n";
    for (const auto& header : headers) {
        resp << header.first << ": " << header.second << "\r\n";
    }
    resp << "Connection: close\r\n"
          << "\r\n"
          << body;
    return resp.str();
}

} // namespace containercp::api
