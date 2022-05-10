/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <type_traits>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

/**
 * This is a template helper to add bindings for a set of implementation
 * classes that may each be part of multiple multibindings. To be more specific,
 * for these example classes:
 *
 *   class ImplementationA : public IntX, IntY {};
 *   class ImplementationB : public IntY, IntZ {};
 *
 * can be installed with
 *
 *   using Deps = fruit::Required<...>;
 *   using Bases = Multibindings<Deps>::Bases<IntX, IntY, IntZ>;
 *   return fruit::createComponent()
 *     .install(Bases::Impls<ImplementationA, ImplementationB>);
 *
 * Note that not all implementations have to implement all interfaces. Invalid
 * combinations are filtered out at compile-time through SFINAE.
 */
template <typename Deps>
struct Multibindings {
  /* SFINAE logic for an individual interface binding. The class does implement
   * the interface, so add a multibinding. */
  template <typename Base, typename Impl,
            std::enable_if_t<std::is_base_of<Base, Impl>::value, bool> = true>
  static fruit::Component<Deps> OneBaseOneImpl() {
    return fruit::createComponent().addMultibinding<Base, Impl>();
  }
  /* SFINAE logic for an individual interface binding. The class does not
   * implement the interface, so do not add a multibinding. */
  template <typename Base, typename Impl,
            std::enable_if_t<!std::is_base_of<Base, Impl>::value, bool> = true>
  static fruit::Component<Deps> OneBaseOneImpl() {
    return fruit::createComponent();
  }

  template <typename Base>
  struct OneBase {
    template <typename... ImplTypes>
    static fruit::Component<Deps> Impls() {
      return fruit::createComponent().installComponentFunctions(
          fruit::componentFunction(OneBaseOneImpl<Base, ImplTypes>)...);
    }
  };

  template <typename... BaseTypes>
  struct Bases {
    template <typename... ImplTypes>
    static fruit::Component<Deps> Impls() {
      return fruit::createComponent().installComponentFunctions(
          fruit::componentFunction(
              OneBase<BaseTypes>::template Impls<ImplTypes...>)...);
    }
  };
};

class LateInjected {
 public:
  virtual ~LateInjected() = default;
  virtual Result<void> LateInject(fruit::Injector<>& injector) = 0;
};

}  // namespace cuttlefish
