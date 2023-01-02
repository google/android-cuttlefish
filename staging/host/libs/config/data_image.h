#pragma once

#include <string>

#include <fruit/fruit.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

class InitializeDataImage : public SetupFeature {};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InitializeDataImage>
InitializeDataImageComponent();

class InitializeEspImage : public SetupFeature {};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 InitializeEspImage>
InitializeEspImageComponent();

bool CreateBlankImage(
    const std::string& image, int num_mb, const std::string& image_fmt);

class InitializeMiscImage : public SetupFeature {};

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InitializeMiscImage>
InitializeMiscImageComponent();

} // namespace cuttlefish
