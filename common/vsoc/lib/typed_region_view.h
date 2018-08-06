#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Object that represents a region on the Host

#include "common/vsoc/lib/region_view.h"

#include <map>
#include <mutex>
#include <string>

namespace vsoc {

/**
 * This class adds methods that depend on the Region's type.
 * This may be directly constructed. However, it may be more effective to
 * subclass it, adding region-specific methods.
 *
 * Layout should be VSoC shared memory compatible, defined in common/vsoc/shm,
 * and should have a constant string region name.
 */
template <typename ViewType, typename LayoutType>
class TypedRegionView : public RegionView {
 public:
  using Layout = LayoutType;

  /* Returns a pointer to the region with a type that matches the layout */
  LayoutType* data() {
    return this->GetLayoutPointer<LayoutType>();
  }

  const LayoutType& data() const {
    return this->region_offset_to_reference<LayoutType>(
        control_->region_desc().offset_of_region_data);
  }

 protected:
#if defined(CUTTLEFISH_HOST)
  bool Open(const char* domain) {
    return RegionView::Open(LayoutType::region_name, domain);
  }
#else
  bool Open() {
    return RegionView::Open(LayoutType::region_name);
  }
#endif

 public:
  // Implementation of the region singletons.
#if defined(CUTTLEFISH_HOST)
  static ViewType* GetInstance(const char* domain) {
    static std::mutex mtx;
    static std::map<std::string, std::unique_ptr<ViewType>> instances;
    if (!domain) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lock(mtx);
    // Get a reference to the actual unique_ptr that's stored in the map, if
    // there wasn't one it will be default constructed pointing to nullptr.
    auto& instance = instances[domain];
    if (!instance) {
      // Update the referenced pointer with the address of the newly created
      // region view.
      instance.reset(new ViewType{});
      if (!instance->Open(domain)) {
        instance.reset();
      }
    }
    return instance.get();
  }
#else
  static ViewType* GetInstance() {
    static std::mutex mtx;
    static std::unique_ptr<ViewType> instance;
    std::lock_guard<std::mutex> lock(mtx);
    if (!instance) {
      instance.reset(new ViewType{});
      if (!instance->Open()) {
        instance.reset();
      }
    }
    return instance.get();
  }
#endif


};

}  // namespace vsoc
