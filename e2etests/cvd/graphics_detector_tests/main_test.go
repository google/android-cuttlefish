// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestLaunchingWithAutoEnablesGfxstream(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	if err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	if err := c.RunAdbWaitForDevice(); err != nil {
		t.Fatalf("failed to wait for Cuttlefish device to connect to adb: %w", err)
	}

	gl_driver, err := c.GetSyspropString("ro.hardware.egl")
	if err != nil {
		t.Fatalf("failed to get EGL sysprop: %w", err)
	}
	if gl_driver != "angle" {
		t.Errorf(`"ro.hardware.egl" was "%s"; expected "angle"`, gl_driver)
	}

	vk_driver, err := c.GetSyspropString("ro.hardware.vulkan")
	if err != nil {
		t.Fatalf("failed to get Vulkan sysprop: %w", err)
	}
	if vk_driver != "ranchu" {
		t.Errorf(`"ro.hardware.vulkan" was "%s"; expected "ranchu"`, vk_driver)
	}
}
