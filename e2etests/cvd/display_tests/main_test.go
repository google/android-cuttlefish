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

func addDisplay(c e2etests.TestContext, t *testing.T) {
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

	if _, err := c.RunCmd("cvd", "display", "add", "--display=width=500,height=500",); err != nil {
		t.Fatal(err)
	}
}

func listDisplays(c e2etests.TestContext, t *testing.T) {
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

	if _, err := c.RunCmd("cvd", "display", "list",); err != nil {
		t.Fatal(err)
	}
}

func TestRunAll(t *testing.T) {
	c := e2etests.TestContext{}
	t.Run("AddDisplay", func(t *testing.T) { addDisplay(c, t) })
	t.Run("ListDisplays", func(t *testing.T) { listDisplays(c, t) })
}
