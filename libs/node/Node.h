#ifndef CONTAINERCP_NODE_NODE_H
#define CONTAINERCP_NODE_NODE_H

#include "core/Resource.h"

#include <string>

namespace containercp::node {

struct Node : core::Resource {
    std::string type;
};

} // namespace containercp::node

#endif // CONTAINERCP_NODE_NODE_H
