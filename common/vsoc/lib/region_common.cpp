#include "common/vsoc/lib/region.h"

#include <sys/mman.h>

vsoc::RegionBase::~RegionBase() {
  if (region_base_ && (region_base_ != MAP_FAILED)) {
    munmap(region_base_, region_size());
  }
}
