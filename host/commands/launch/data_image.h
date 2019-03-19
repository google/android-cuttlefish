#pragma once

#include <string>

#include "host/libs/config/cuttlefish_config.h"

bool ApplyDataImagePolicy(const vsoc::CuttlefishConfig& config);
void CreateBlankImage(
    const std::string& image, int image_mb, const std::string& image_fmt);
