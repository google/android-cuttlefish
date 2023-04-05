/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/server_command/acloud.h"

#include <atomic>

#include <fruit/fruit.h>

#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/server_command/acloud.h"
#include "host/commands/cvd/server_command/acloud_command.h"
#include "host/commands/cvd/server_command/acloud_translator.h"
#include "host/commands/cvd/server_command/try_acloud.h"

namespace cuttlefish {

fruit::Component<fruit::Required<
    CommandSequenceExecutor,
    fruit::Annotated<AcloudTranslatorOptOut, std::atomic<bool>>>>
CvdAcloudComponent() {
  return fruit::createComponent()
      .install(AcloudCreateConvertCommandComponent)
      .install(AcloudCommandComponent)
      .install(TryAcloudCommandComponent)
      .install(AcloudTranslatorCommandComponent);
}

}  // namespace cuttlefish
