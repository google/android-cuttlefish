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
	"strings"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestLaunchingWithAutoEnablesGfxstream(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)

	if err := c.CVDFetch(e2etests.FetchArgs{
		DefaultBuildBranch: "aosp-android-latest-release",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-userdebug",
	}); err != nil {
		t.Fatal(err)
	}

	if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
		t.Fatal(err)
	}

	output, err := c.RunCmd("adb", "shell", "getprop", "ro.hardware.egl")
	if err != nil {
		t.Fatalf("failed to get EGL sysprop: %w", err)
	}
	output = strings.TrimSpace(output)
	if output != "emulation" {
		t.Errorf(`"ro.hardware.egl" was "%s"; expected "emulation"`, output)
	}

	c.TearDown()
}
