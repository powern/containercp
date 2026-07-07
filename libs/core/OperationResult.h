#ifndef CONTAINERCP_CORE_OPERATION_RESULT_H
#define CONTAINERCP_CORE_OPERATION_RESULT_H

#include <string>

namespace containercp::core {

struct OperationResult {
    bool success = false;
    std::string message;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_OPERATION_RESULT_H
