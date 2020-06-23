#pragma once

#include <string>

#include "host/libs/config/cuttlefish_config.h"

enum class DataImageResult {
  Error,
  NoChange,
  FileUpdated,
};

DataImageResult ApplyDataImagePolicy(const cuttlefish::CuttlefishConfig& config,
                                     const std::string& path);
bool InitializeMiscImage(const std::string& misc_image);
void CreateBlankImage(
    const std::string& image, int num_mb, const std::string& image_fmt);
