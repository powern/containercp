#ifndef CONTAINERCP_CORE_PROGRESS_CALLBACK_H
#define CONTAINERCP_CORE_PROGRESS_CALLBACK_H

#include <functional>
#include <string>

namespace containercp::core {

using ProgressCallback = std::function<void(int percent, const std::string& step)>;

inline void noop_progress(int, const std::string&) {}

} // namespace containercp::core

#endif // CONTAINERCP_CORE_PROGRESS_CALLBACK_H
