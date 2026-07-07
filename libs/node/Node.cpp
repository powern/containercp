#include "Node.h"

namespace containercp::node {

Node get_default_node() {
    Node n;
    n.name = "local";
    n.type = "local";
    return n;
}

Node find_node(const std::string& name) {
    if (name == "local") {
        Node n;
        n.name = "local";
        n.type = "local";
        return n;
    }
    return Node{};
}

} // namespace containercp::node
