/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/vsoc/lib/e2e_test_region_view.h"

namespace vsoc {

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<E2EPrimaryRegionView> E2EPrimaryRegionView::GetInstance(
    const char* domain) {
  return vsoc::RegionView::GetInstanceImpl<E2EPrimaryRegionView>(
      [](std::shared_ptr<E2EPrimaryRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<E2EPrimaryRegionView> E2EPrimaryRegionView::GetInstance() {
  return vsoc::RegionView::GetInstanceImpl<E2EPrimaryRegionView>(
      std::mem_fn(&E2EPrimaryRegionView::Open));
}
#endif

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<E2ESecondaryRegionView> E2ESecondaryRegionView::GetInstance(
    const char* domain) {
  return vsoc::RegionView::GetInstanceImpl<E2ESecondaryRegionView>(
      [](std::shared_ptr<E2ESecondaryRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<E2ESecondaryRegionView> E2ESecondaryRegionView::GetInstance() {
  return vsoc::RegionView::GetInstanceImpl<E2ESecondaryRegionView>(
      std::mem_fn(&E2ESecondaryRegionView::Open));
}
#endif

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<E2EUnfindableRegionView> E2EUnfindableRegionView::GetInstance(
    const char* domain) {
  return vsoc::RegionView::GetInstanceImpl<E2EUnfindableRegionView>(
      [](std::shared_ptr<E2EUnfindableRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<E2EUnfindableRegionView>
E2EUnfindableRegionView::GetInstance() {
  return vsoc::RegionView::GetInstanceImpl<E2EUnfindableRegionView>(
      std::mem_fn(&E2EUnfindableRegionView::Open));
}
#endif

}  // namespace vsoc
