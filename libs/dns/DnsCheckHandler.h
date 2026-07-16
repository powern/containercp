#ifndef CONTAINERCP_DNS_DNS_CHECK_HANDLER_H
#define CONTAINERCP_DNS_DNS_CHECK_HANDLER_H

#include "api/Request.h"
#include "api/Response.h"

namespace containercp {
namespace network { class NetworkService; }
namespace dns {

class DnsCheckService;

api::Response handleDnsCheck(
    const api::Request& req,
    DnsCheckService& svc,
    containercp::network::NetworkService* net = nullptr);

} // namespace containercp::dns
} // namespace containercp

#endif // CONTAINERCP_DNS_DNS_CHECK_HANDLER_H
