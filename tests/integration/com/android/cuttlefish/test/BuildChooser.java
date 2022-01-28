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
package com.android.cuttlefish.test;

import com.android.tradefed.build.IBuildInfo;
import com.google.inject.Inject;
import javax.annotation.Nullable;

/**
 * Manager for a dedicated GCE instance for every @Test function.
 *
 * Must be constructed through Guice injection. Calls out to the cvd_test_gce_driver binary to
 * create the GCE instances.
 */
public final class BuildChooser {
  @Inject(optional = true)
  @SetOption("build-flavor")
  @Nullable
  private String buildFlavorOption = null;

  @Inject(optional = true) @SetOption("build-id") @Nullable private String buildIdOption = null;

  @Inject private IBuildInfo buildInfo;

  public String fetchCvdBuild() {
    return buildId() + "/" + buildFlavor();
  }

  public Build buildProto() {
    return Build.newBuilder().setId(buildId()).setTarget(buildFlavor()).build();
  }

  public String buildFlavor() {
    if (buildFlavorOption != null) {
      return buildFlavorOption;
    }
    return buildInfo.getBuildFlavor();
  }

  public String buildId() {
    if (buildIdOption != null) {
      return buildIdOption;
    }
    return buildInfo.getBuildId();
  }
}
