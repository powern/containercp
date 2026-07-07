#ifndef CONTAINERCP_NODE_NODE_H
#define CONTAINERCP_NODE_NODE_H

#include "core/Resource.h"

#include <string>

namespace containercp::node {

struct Node : core::Resource {
    std::string type;
};

Node get_default_node();
Node find_node(const std::string& name);

} // namespace containercp::node

#endif // CONTAINERCP_NODE_NODE_H
