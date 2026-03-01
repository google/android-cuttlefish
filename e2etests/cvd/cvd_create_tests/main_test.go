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
	"fmt"
	"testing"

	"github.com/google/android-cuttlefish/e2etests/cvd/common"
)

func TestCvdCreate(t *testing.T) {
	testcases := []struct {
		branch string
		target string
	}{
		{
			branch: "aosp-android-latest-release",
			target: "aosp_cf_x86_64_only_phone-userdebug",
		},
		{
			branch: "git_main",
			target: "aosp_cf_x86_64_only_phone-trunk_staging-userdebug",
		},
	}
	for _, tc := range testcases {
		t.Run(fmt.Sprintf("BUILD=%s/%s", tc.branch, tc.target), func(t *testing.T) {
			c := e2etests.TestContext{}
			c.SetUp(t)

			if err := c.CVDFetch(e2etests.FetchArgs{
				DefaultBuildBranch: tc.branch,
				DefaultBuildTarget: tc.target,
			}); err != nil {
				t.Fatal(err)
			}

			if err := c.CVDCreate(e2etests.CreateArgs{}); err != nil {
				t.Fatal(err)
			}

			c.TearDown()
		})
	}
}
