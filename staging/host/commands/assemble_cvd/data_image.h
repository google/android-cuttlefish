#pragma once

#include <string>

#include "host/libs/config/cuttlefish_config.h"

enum class DataImageResult {
  Error,
  NoChange,
  FileUpdated,
};

DataImageResult ApplyDataImagePolicy(const vsoc::CuttlefishConfig& config,
                                     const std::string& path);
bool InitializeMiscImage(const std::string& misc_image);
void CreateBlankImage(
    const std::string& image, int block_count, const std::string& image_fmt,
    const std::string& block_size = "1M");
