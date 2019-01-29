#pragma once

#include <string>

bool ApplyDataImagePolicy(const char* data_image,
                          const std::string& data_policy,
                          int blank_data_image_mb,
                          const std::string& blank_data_image_fmt);
