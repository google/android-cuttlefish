#pragma once

#include <aom/aomcx.h>

#include <stdlib.h>

aom_codec_iface_t *aom_codec_av1_cx(void) {
  // external/libaom doesn't include encoder sources, which makes this function
  // not available. Defined here to make it build, but ensure it fails fast if
  // called in runtime.
  abort();
}
