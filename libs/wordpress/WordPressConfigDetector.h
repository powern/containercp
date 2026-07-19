#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H

#include "WordPressConfigTypes.h"

#include <string>

namespace containercp::wordpress {

class WordPressConfigDetector {
public:
    WordPressConfigInspection inspect_content(const std::string& content) const;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H
