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

func TestCvdCreateSubstituteOnly(t *testing.T) {
	c := e2etests.TestContext{}
	c.SetUp(t)
	defer c.TearDown()

	fetch_args := e2etests.FetchArgs{
		BootloaderBuildBranch: "aosp_u-boot-mainline",
		BootloaderBuildTarget: "u-boot_crosvm_x86_64",
		DefaultBuildBranch: "git_main",
		DefaultBuildTarget: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		SubstituteOnly: true,
	}

	if _, err := c.CVDFetch(fetch_args); err != nil {
		t.Fatal(err)
	}

	create_args := e2etests.CreateArgs{
		Args: []string {
			"--enable_wifi=false",
		},
	}

	if _, err := c.CVDCreate(create_args); err != nil {
		t.Fatal(err)
	}
}
