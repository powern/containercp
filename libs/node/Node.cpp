#include "Node.h"

namespace containercp::node {

Node get_default_node() {
    return Node{"local", "local"};
}

Node find_node(const std::string& name) {
    if (name == "local") {
        return Node{"local", "local"};
    }
    return Node{};
}

} // namespace containercp::node
