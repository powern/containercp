#ifndef CONTAINERCP_NODE_NODE_H
#define CONTAINERCP_NODE_NODE_H

#include <string>

namespace containercp::node {

struct Node {
    std::string name;
    std::string type;
};

Node get_default_node();
Node find_node(const std::string& name);

} // namespace containercp::node

#endif // CONTAINERCP_NODE_NODE_H
